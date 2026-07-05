#include "ping_manager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <print>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "icmplib.h"

namespace {

bool hasRawSocketCapability() {
#ifdef _WIN32
    return false;
#else
    return geteuid() == 0;
#endif
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::tuple<std::string, std::string, bool, short, std::string>
pingHostRaw(const std::string& ip, const std::string& hostname,
            int pingCount, int timeoutSeconds) {
    std::vector<short> delays;
    bool success = false;

    unsigned timeoutMs = static_cast<unsigned>(timeoutSeconds) * 1000;
    icmplib::IPAddress target(ip);

    for (int i = 0; i < pingCount; ++i) {
        auto result = icmplib::Ping(target, timeoutMs, static_cast<uint16_t>(i + 1));
        if (result.response == icmplib::PingResult::ResponseType::Success) {
            success = true;
        }
        delays.push_back(static_cast<short>(result.delay));
    }

    short minDelay = *std::ranges::min_element(delays);
    return std::make_tuple(ip, hostname, success, minDelay, getCurrentTimestamp());
}

// 使用系统 ping 命令，一次 fork 完成所有包探测
// 通过解析 ping 输出获取最小延迟
std::tuple<std::string, std::string, bool, short, std::string>
pingHostSystem(const std::string& ip, const std::string& hostname,
               int pingCount, int timeoutSeconds) {
    // 构建命令：ping -c N -W timeout ip，stderr 重定向到 /dev/null
    std::string cmd = "ping -c " + std::to_string(pingCount)
                    + " -W " + std::to_string(timeoutSeconds)
                    + " " + ip + " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return std::make_tuple(ip, hostname, false,
                               static_cast<short>(timeoutSeconds * 1000),
                               getCurrentTimestamp());
    }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        output += buf;
    }

    int rawStatus = pclose(pipe);
    bool success = (rawStatus != -1 && WIFEXITED(rawStatus) && WEXITSTATUS(rawStatus) == 0);
    short delay = static_cast<short>(timeoutSeconds * 1000);

    if (success) {
        // 解析 rtt 行获取最小延迟
        // "rtt min/avg/max/mdev = 12.345/67.890/12.345/1.234 ms"
        auto pos = output.rfind("rtt min/avg/max/mdev");
        if (pos != std::string::npos) {
            auto eq = output.find('=', pos);
            if (eq != std::string::npos) {
                auto slash = output.find('/', eq + 1);
                if (slash != std::string::npos) {
                    size_t start  = eq + 2; // 跳过 "= "
                    std::string minStr = output.substr(start, slash - start);
                    try {
                        double ms = std::stod(minStr);
                        delay = static_cast<short>(std::round(ms));
                    } catch (...) {}
                }
            }
        }
    }

    return std::make_tuple(ip, hostname, success, delay, getCurrentTimestamp());
}

}  // namespace

std::tuple<std::string, std::string, bool, short, std::string> pingHost(const std::string& ip,
                                                                        const std::string& hostname,
                                                                        int pingCount,
                                                                        int timeoutSeconds) {
    if (hasRawSocketCapability()) {
        return pingHostRaw(ip, hostname, pingCount, timeoutSeconds);
    }
    return pingHostSystem(ip, hostname, pingCount, timeoutSeconds);
}

std::vector<std::tuple<std::string, std::string, bool, short, std::string>>
PingManager::performPingInternal(const std::map<std::string, std::string>& hosts, int pingCount,
                                 int timeoutSeconds, size_t maxConcurrent) {
    if (hosts.empty()) {
        return {};
    }

    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults;
    allResults.reserve(hosts.size());

    std::mutex resultsMutex;

    // 将所有主机放入队列
    std::queue<std::pair<std::string, std::string>> hostQueue;
    for (const auto& [ip, hostname] : hosts) {
        hostQueue.emplace(ip, hostname);
    }

    size_t actualConcurrent = std::min(maxConcurrent, hosts.size());
    std::vector<std::jthread> workers;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop(false);

    std::atomic<size_t> activeTasks(hosts.size());
    std::mutex completionMutex;
    std::condition_variable completionCV;

    for (size_t i = 0; i < actualConcurrent; ++i) {
        workers.emplace_back([&, pingCount, timeoutSeconds]() {
            while (true) {
                std::pair<std::string, std::string> host;

                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [&] { return stop.load() || !hostQueue.empty(); });

                    if (stop.load() && hostQueue.empty()) {
                        return;
                    }

                    if (!hostQueue.empty()) {
                        host = hostQueue.front();
                        hostQueue.pop();
                    } else {
                        continue;
                    }
                }

                auto result = pingHost(host.first, host.second, pingCount, timeoutSeconds);

                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    allResults.push_back(result);
                }

                if (--activeTasks == 0) {
                    completionCV.notify_one();
                }
            }
        });
    }

    {
        std::unique_lock<std::mutex> lock(completionMutex);
        completionCV.wait(lock, [&] { return activeTasks.load() == 0; });
    }

    stop.store(true);
    condition.notify_all();

    return allResults;
}

std::vector<std::tuple<std::string, std::string, bool, short, std::string>>
PingManager::performPing(const std::map<std::string, std::string>& hosts,
                         size_t maxConcurrent) {
    auto allResults = performPingInternal(hosts, ConfigDefaults::FIRST_ROUND_PING_COUNT, ConfigDefaults::FIRST_ROUND_TIMEOUT, maxConcurrent);

    // 收集第一轮失败的主机
    std::map<std::string, std::string> failedHosts;
    for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
        if (!success) {
            failedHosts[ip] = hostname;
        }
    }

    // 对失败的主机开启第二轮 ping 检查，保证结果正确
    if (!failedHosts.empty()) {
        auto retryResults =
            performPingInternal(failedHosts, ConfigDefaults::RETRY_ROUND_PING_COUNT, ConfigDefaults::RETRY_ROUND_TIMEOUT, maxConcurrent);

        for (auto& result : allResults) {
            const std::string& ip = std::get<0>(result);
            for (const auto& retry : retryResults) {
                if (std::get<0>(retry) == ip) {
                    result = retry;
                    break;
                }
            }
        }
    }

    return allResults;
}
