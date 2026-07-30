#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "commands.h"
#include "constants.h"

class PingManager {
   public:
    PingManager();
    ~PingManager();

    std::vector<PingResult> performPing(
        const std::map<std::string, std::string>& hosts,
        size_t maxConcurrent = ConfigDefaults::MAX_CONCURRENT_PINGS);

   private:
    static const size_t DEFAULT_MAX_CONCURRENT = ConfigDefaults::MAX_CONCURRENT_PINGS;

    std::vector<PingResult> performPingInternal(const std::map<std::string, std::string>& hosts,
                                                int pingCount, int timeoutSeconds,
                                                size_t maxConcurrent);

    // 可复用的线程池
    std::vector<std::jthread> workers;
    std::queue<std::function<void()>> taskQueue;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};

    void workerLoop();
    void ensureThreadCount(size_t needed);
};

#endif  // PING_MANAGER_H