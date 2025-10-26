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

class PingManager {
public:
    // 执行ping操作，返回结果列表
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> performPing(const std::map<std::string, std::string>& hosts, int pingCount = 3);
};

#endif // PING_MANAGER_H