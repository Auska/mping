#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <future>
#include <chrono>
#include <sstream>
#include <iomanip>

class PingManager {
public:
    // 执行ping操作并返回结果
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> performPing(const std::map<std::string, std::string>& hosts);
};

#endif // PING_MANAGER_H