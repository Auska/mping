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

    // 滑动窗口检查：取代原先"两轮 ping + 告警确认重试"的重叠流程。
    // 每轮并发检查仍待判定主机（每轮每主机发 CHECK_ROUND_PING_COUNT 个探测包），
    // 任一轮成功即判定在线；连续 windowSize 轮全部失败才判定离线（滑动窗口对抗网络波动）。
    // 轮次有界：在线主机 1 轮出结果，离线主机恰好 windowSize 轮。
    std::vector<PingResult> checkHosts(const std::map<std::string, std::string>& hosts,
                                       int windowSize,
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