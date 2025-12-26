#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <vector>
#include <string>
#include <map>
#include <tuple>
#include <future>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

class PingManager {
private:
    // 默认最大并发数
    static const size_t DEFAULT_MAX_CONCURRENT = 50;
    
public:
    // 执行ping操作，返回结果列表
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> performPing(
        const std::map<std::string, std::string>& hosts, 
        int pingCount = 3, 
        int timeoutSeconds = 3,
        size_t maxConcurrent = DEFAULT_MAX_CONCURRENT);
};

#endif // PING_MANAGER_H