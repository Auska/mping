#include "ping_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <print>
#include <unordered_map>
#include <vector>

#include "commands.h"
#include "icmplib.h"

namespace {

std::string getCurrentTimestamp() {
    // sys_seconds 原生以 UTC 表示，直接格式化，无需 gmtime_r/stringstream
    return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::time_point_cast<std::chrono::seconds>(
                                                   std::chrono::system_clock::now()));
}

PingResult pingHost(const std::string& ip, const std::string& hostname, int pingCount,
                    int timeoutSeconds) {
    bool success   = false;
    short minDelay = static_cast<short>(timeoutSeconds * 1000);

    try {
        unsigned timeoutMs = static_cast<unsigned>(timeoutSeconds) * 1000;
        // 不合法 IP 也在此捕获并按不可达处理，保证每台主机都有检查结果（窗口循环依赖该保证）
        icmplib::IPAddress target(ip);
        // 复用同一 raw socket 连续发包，避免每包重复创建/关闭 socket
        icmplib::PingSocket socket(target.GetType());
        for (int i = 0; i < pingCount; ++i) {
            auto result =
                icmplib::Ping(socket.GetSocket(), target, timeoutMs, static_cast<uint16_t>(i + 1));
            if (result.response == icmplib::PingResult::ResponseType::Success) {
                success  = true;
                minDelay = std::min(minDelay, static_cast<short>(result.delay));
            }
        }
    } catch (const std::exception&) {
        // socket 创建/发包失败按主机不可达处理（与原行为一致）
    }

    return {.ip        = ip,
            .hostname  = hostname,
            .success   = success,
            .delayMs   = minDelay,
            .timestamp = getCurrentTimestamp()};
}

}  // namespace

PingManager::~PingManager() {
    stop.store(true);
    condition.notify_all();
    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }
}

void PingManager::ensureThreadCount(size_t needed) {
    size_t target = std::min(std::max(size_t{1}, needed),
                             static_cast<size_t>(ConfigDefaults::MAX_CONCURRENT_PINGS));
    while (workers.size() < target) {
        workers.emplace_back([this] { workerLoop(); });
    }
}

void PingManager::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stop.load() || !taskQueue.empty(); });
            if (stop.load() && taskQueue.empty()) {
                return;
            }
            task = std::move(taskQueue.front());
            taskQueue.pop();
        }
        try {
            task();
        } catch (const std::exception& e) {
            std::println(std::cerr, "Ping task threw an exception: {}", e.what());
        } catch (...) {
            std::println(std::cerr, "Ping task threw an unknown exception");
        }
    }
}

std::vector<PingResult> PingManager::performPingInternal(
    const std::map<std::string, std::string>& hosts, int pingCount, int timeoutSeconds,
    size_t maxConcurrent) {
    if (hosts.empty()) {
        return {};
    }

    ensureThreadCount(std::min(maxConcurrent, hosts.size()));

    std::vector<PingResult> allResults;
    allResults.reserve(hosts.size());

    std::mutex resultsMutex;
    size_t activeTasks = hosts.size();
    std::mutex completionMutex;
    std::condition_variable completionCV;

    for (const auto& [ip, hostname] : hosts) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            taskQueue.push([ip, hostname, pingCount, timeoutSeconds, &allResults, &resultsMutex,
                            &activeTasks, &completionMutex, &completionCV] {
                // RAII：无论 pingHost 是否抛异常都递减完成计数，避免主线程永久等待
                struct CompletionGuard {
                    std::mutex& mutex;
                    size_t& activeTasks;
                    std::condition_variable& cv;
                    ~CompletionGuard() {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (--activeTasks == 0) {
                            cv.notify_one();
                        }
                    }
                } guard{completionMutex, activeTasks, completionCV};

                auto result = pingHost(ip, hostname, pingCount, timeoutSeconds);
                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    allResults.push_back(result);
                }
            });
        }
        condition.notify_one();
    }

    {
        std::unique_lock<std::mutex> lock(completionMutex);
        completionCV.wait(lock, [&] { return activeTasks == 0; });
    }

    return allResults;
}

std::vector<PingResult> PingManager::checkHosts(const std::map<std::string, std::string>& hosts,
                                                int windowSize, size_t maxConcurrent) {
    if (hosts.empty() || windowSize < 1) {
        return {};
    }

    // 滑动窗口确认，对抗瞬时网络波动：
    // - 任一轮成功 → 立即判定该主机在线（快路径）
    // - 连续 windowSize 轮全部失败 → 判定离线（窗口内不允许任何成功）
    // 轮次有界：在线主机 1 轮出结果，离线主机恰好 windowSize 轮。
    std::map<std::string, std::string> pending = hosts;
    std::unordered_map<std::string, PingResult> finalResults;
    std::unordered_map<std::string, size_t> consecutiveFailures;
    finalResults.reserve(hosts.size());

    while (!pending.empty()) {
        auto roundResults = performPingInternal(pending, ConfigDefaults::CHECK_ROUND_PING_COUNT,
                                                ConfigDefaults::CHECK_ROUND_TIMEOUT, maxConcurrent);

        std::unordered_map<std::string, PingResult> roundByIp;
        roundByIp.reserve(roundResults.size());
        for (auto& r : roundResults) {
            roundByIp[r.ip] = std::move(r);
        }

        for (auto it = pending.begin(); it != pending.end();) {
            const std::string& ip = it->first;
            auto rit              = roundByIp.find(ip);

            if (rit != roundByIp.end() && rit->second.success) {
                finalResults[ip] = std::move(rit->second);
                it               = pending.erase(it);
                consecutiveFailures.erase(ip);
                continue;
            }

            // 本轮失败（或异常无结果，视为失败）→ 进入窗口继续累计
            ++consecutiveFailures[ip];
            if (consecutiveFailures[ip] < static_cast<size_t>(windowSize)) {
                ++it;
                continue;
            }

            // 连续 windowSize 轮失败 → 判定离线
            if (rit != roundByIp.end()) {
                finalResults[ip] = std::move(rit->second);
            }
            it = pending.erase(it);
            consecutiveFailures.erase(ip);
        }
    }

    // 按输入顺序组织输出，保证结果顺序稳定
    std::vector<PingResult> results;
    results.reserve(hosts.size());
    for (const auto& host : hosts) {
        auto it = finalResults.find(host.first);
        if (it != finalResults.end()) {
            results.push_back(std::move(it->second));
        }
    }
    return results;
}
