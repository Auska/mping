#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <chrono>
#include <condition_variable>
#include <future>
#include <iomanip>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

class PingManager {
   private:
    // 默认最大并发数
    static const size_t DEFAULT_MAX_CONCURRENT = 50;

   public:
    // 执行ping操作，返回结果列表
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> performPing(
        const std::map<std::string, std::string>& hosts, int pingCount = 3, int timeoutSeconds = 3,
        size_t maxConcurrent = DEFAULT_MAX_CONCURRENT);
};

#endif  // PING_MANAGER_H