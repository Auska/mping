#include "ping_manager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <print>
#include <ranges>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "commands.h"
#include "icmplib.h"

namespace {

bool hasRawSocketCapability() {
    return geteuid() == 0;
}

std::string getCurrentTimestamp() {
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

PingResult pingHostRaw(const std::string& ip, const std::string& hostname, int pingCount,
                       int timeoutSeconds) {
    bool success   = false;
    short minDelay = std::numeric_limits<short>::max();

    unsigned timeoutMs = static_cast<unsigned>(timeoutSeconds) * 1000;
    icmplib::IPAddress target(ip);

    for (int i = 0; i < pingCount; ++i) {
        auto result = icmplib::Ping(target, timeoutMs, static_cast<uint16_t>(i + 1));
        if (result.response == icmplib::PingResult::ResponseType::Success) {
            success = true;
        }
        minDelay = std::min(minDelay, static_cast<short>(result.delay));
    }

    return {.ip        = ip,
            .hostname  = hostname,
            .success   = success,
            .delayMs   = minDelay,
            .timestamp = getCurrentTimestamp()};
}

PingResult pingHostSystem(const std::string& ip, const std::string& hostname, int pingCount,
                          int timeoutSeconds) {
    std::string cmd = "LANG=C ping -c " + std::to_string(pingCount) + " -W "
                      + std::to_string(timeoutSeconds) + " " + ip + " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {.ip        = ip,
                .hostname  = hostname,
                .success   = false,
                .delayMs   = static_cast<short>(timeoutSeconds * 1000),
                .timestamp = getCurrentTimestamp()};
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
                    } catch (const std::exception& e) {
                        std::println(std::cerr, "Warning: Failed to parse ping delay '{}': {}",
                                     minStr, e.what());
                    }
                }
            }
        }
    }

    return {.ip        = ip,
            .hostname  = hostname,
            .success   = success,
            .delayMs   = delay,
            .timestamp = getCurrentTimestamp()};
}

}  // namespace

PingResult pingHost(const std::string& ip, const std::string& hostname, int pingCount,
                    int timeoutSeconds) {
    if (hasRawSocketCapability()) {
        return pingHostRaw(ip, hostname, pingCount, timeoutSeconds);
    }
    return pingHostSystem(ip, hostname, pingCount, timeoutSeconds);
}

PingManager::PingManager() {
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

void PingManager::ensureThreadCount(size_t needed) {
    size_t target = std::min(std::max(size_t{1}, needed), DEFAULT_MAX_CONCURRENT);
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
    size_t /*maxConcurrent*/) {
    if (hosts.empty()) {
        return {};
    }

    ensureThreadCount(hosts.size());

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
                auto result = pingHost(ip, hostname, pingCount, timeoutSeconds);
                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    allResults.push_back(result);
                }
                {
                    std::lock_guard<std::mutex> lock(completionMutex);
                    if (--activeTasks == 0) {
                        completionCV.notify_one();
                    }
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
