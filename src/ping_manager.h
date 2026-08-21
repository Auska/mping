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
    ~PingManager();

    std::vector<PingResult> performPing(
        const std::map<std::string, std::string>& hosts,
        size_t maxConcurrent = ConfigDefaults::MAX_CONCURRENT_PINGS);

    // 对指定主机执行多轮重试确认：任一轮成功即停止该主机的重试，
    // 返回每台主机的最终检查结果（全部重试轮次结束后）
    std::vector<PingResult> retryHosts(const std::map<std::string, std::string>& hosts,
                                       int retryCount,
                                       size_t maxConcurrent = ConfigDefaults::MAX_CONCURRENT_PINGS);

   private:
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