#include "ping_manager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
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
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::tuple<std::string, std::string, bool, short, std::string> pingHostRaw(
    const std::string& ip, const std::string& hostname, int pingCount, int timeoutSeconds) {
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

std::tuple<std::string, std::string, bool, short, std::string> pingHostSystem(
    const std::string& ip, const std::string& hostname, int pingCount, int timeoutSeconds) {
    std::string cmd = "LANG=C ping -c " + std::to_string(pingCount) + " -W "
                      + std::to_string(timeoutSeconds) + " " + ip + " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return std::make_tuple(ip, hostname, false, static_cast<short>(timeoutSeconds * 1000),
                               getCurrentTimestamp());
    }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        output += buf;
    }

    int rawStatus = pclose(pipe);
    bool success  = (rawStatus != -1 && WIFEXITED(rawStatus) && WEXITSTATUS(rawStatus) == 0);
    short delay   = static_cast<short>(timeoutSeconds * 1000);

    if (success) {
        auto pos = output.rfind("rtt min/avg/max/mdev");
        if (pos != std::string::npos) {
            auto eq = output.find('=', pos);
            if (eq != std::string::npos) {
                auto slash = output.find('/', eq + 1);
                if (slash != std::string::npos) {
                    size_t start       = eq + 2;
                    std::string minStr = output.substr(start, slash - start);
                    try {
                        double ms = std::stod(minStr);
                        delay     = static_cast<short>(std::round(ms));
                    } catch (...) {
                    }
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

PingManager::PingManager() {
    size_t threadCount = std::max(size_t{1}, DEFAULT_MAX_CONCURRENT);
    for (size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back([this] { workerLoop(); });
    }
}

PingManager::~PingManager() {
    stop.store(true);
    condition.notify_all();
    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
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
        task();
    }
}

std::vector<std::tuple<std::string, std::string, bool, short, std::string>>
PingManager::performPingInternal(const std::map<std::string, std::string>& hosts, int pingCount,
                                 int timeoutSeconds, size_t /*maxConcurrent*/) {
    if (hosts.empty()) {
        return {};
    }

    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults;
    allResults.reserve(hosts.size());

    std::mutex resultsMutex;
    std::atomic<size_t> activeTasks(hosts.size());
    std::mutex completionMutex;
    std::condition_variable completionCV;

    for (const auto& [ip, hostname] : hosts) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            taskQueue.push([ip, hostname, pingCount, timeoutSeconds, &allResults, &resultsMutex,
                            &activeTasks, &completionCV] {
                auto result = pingHost(ip, hostname, pingCount, timeoutSeconds);
                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    allResults.push_back(result);
                }
                if (--activeTasks == 0) {
                    completionCV.notify_one();
                }
            });
        }
        condition.notify_one();
    }

    {
        std::unique_lock<std::mutex> lock(completionMutex);
        completionCV.wait(lock, [&] { return activeTasks.load() == 0; });
    }

    return allResults;
}

std::vector<std::tuple<std::string, std::string, bool, short, std::string>>
PingManager::performPing(const std::map<std::string, std::string>& hosts, size_t maxConcurrent) {
    auto allResults = performPingInternal(hosts, ConfigDefaults::FIRST_ROUND_PING_COUNT,
                                          ConfigDefaults::FIRST_ROUND_TIMEOUT, maxConcurrent);

    std::map<std::string, std::string> failedHosts;
    for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
        if (!success) {
            failedHosts[ip] = hostname;
        }
    }

    if (!failedHosts.empty()) {
        auto retryResults = performPingInternal(failedHosts, ConfigDefaults::RETRY_ROUND_PING_COUNT,
                                                ConfigDefaults::RETRY_ROUND_TIMEOUT, maxConcurrent);

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
