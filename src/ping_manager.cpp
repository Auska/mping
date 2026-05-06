#include "ping_manager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <print>
#include <ranges>
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

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");

    return std::make_tuple(ip, hostname, success, minDelay, timestamp.str());
}

std::tuple<std::string, std::string, bool, short, std::string>
pingHostSystem(const std::string& ip, const std::string& hostname,
               int pingCount, int timeoutSeconds) {
    std::vector<short> delays;
    bool success = false;

    for (int i = 0; i < pingCount; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);

            execlp("ping", "ping", "-c", "1", "-W", std::to_string(timeoutSeconds).c_str(),
                   ip.c_str(), (char*)NULL);
            exit(1);
        } else if (pid > 0) {
            auto start = std::chrono::high_resolution_clock::now();
            int status;
            waitpid(pid, &status, 0);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                success = true;
            }

            delays.push_back(static_cast<short>(duration));
        } else {
            std::cerr << "Failed to fork process for ping: " << ip << std::endl;
            delays.push_back(static_cast<short>(timeoutSeconds * 1000));
        }
    }

    short minDelay = *std::ranges::min_element(delays);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");

    return std::make_tuple(ip, hostname, success, minDelay, timestamp.str());
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
