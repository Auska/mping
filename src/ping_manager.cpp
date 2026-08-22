#include "ping_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <print>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "commands.h"
#include "icmplib.h"

namespace {

std::string getCurrentTimestamp() {
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

PingResult pingHost(const std::string& ip, const std::string& hostname, int pingCount,
                    int timeoutSeconds) {
    bool success   = false;
    short minDelay = static_cast<short>(timeoutSeconds * 1000);

    unsigned timeoutMs = static_cast<unsigned>(timeoutSeconds) * 1000;
    icmplib::IPAddress target(ip);

    for (int i = 0; i < pingCount; ++i) {
        auto result = icmplib::Ping(target, timeoutMs, static_cast<uint16_t>(i + 1));
        if (result.response == icmplib::PingResult::ResponseType::Success) {
            success  = true;
            minDelay = std::min(minDelay, static_cast<short>(result.delay));
        }
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

std::vector<PingResult> PingManager::performPing(const std::map<std::string, std::string>& hosts,
                                                 size_t maxConcurrent) {
    auto allResults = performPingInternal(hosts, ConfigDefaults::FIRST_ROUND_PING_COUNT,
                                          ConfigDefaults::FIRST_ROUND_TIMEOUT, maxConcurrent);

    std::map<std::string, std::string> failedHosts;
    for (const auto& result : allResults) {
        if (!result.success) {
            failedHosts[result.ip] = result.hostname;
        }
    }

    if (!failedHosts.empty()) {
        auto retryResults = performPingInternal(failedHosts, ConfigDefaults::RETRY_ROUND_PING_COUNT,
                                                ConfigDefaults::RETRY_ROUND_TIMEOUT, maxConcurrent);

        std::unordered_map<std::string, PingResult> retryMap;
        retryMap.reserve(retryResults.size());
        for (auto& r : retryResults) {
            retryMap[r.ip] = std::move(r);
        }
        for (auto& result : allResults) {
            auto it = retryMap.find(result.ip);
            if (it != retryMap.end()) {
                result = std::move(it->second);
            }
        }
    }

    return allResults;
}

std::vector<PingResult> PingManager::retryHosts(const std::map<std::string, std::string>& hosts,
                                                int retryCount, size_t maxConcurrent) {
    std::vector<PingResult> results;
    if (hosts.empty() || retryCount <= 0) {
        return results;
    }

    std::map<std::string, std::string> pending = hosts;
    std::unordered_map<std::string, PingResult> finalResults;
    finalResults.reserve(hosts.size());

    // 每轮并行检查仍处于失败状态的主机；某主机任一轮成功即停止其重试
    for (int round = 0; round < retryCount && !pending.empty(); ++round) {
        auto roundResults = performPingInternal(pending, ConfigDefaults::RETRY_ROUND_PING_COUNT,
                                                ConfigDefaults::RETRY_ROUND_TIMEOUT, maxConcurrent);
        for (auto& r : roundResults) {
            bool success       = r.success;
            finalResults[r.ip] = std::move(r);
            if (success) {
                pending.erase(r.ip);
            }
        }
    }

    results.reserve(finalResults.size());
    for (auto& [ip, result] : finalResults) {
        results.push_back(std::move(result));
    }
    return results;
}
