#include "ping_manager.h"
#include <iostream>

std::vector<std::tuple<std::string, std::string, bool, short, std::string>> PingManager::performPing(const std::map<std::string, std::string>& hosts) {
    // 使用C++20的特性来并发执行ping操作
    std::vector<std::future<std::tuple<std::string, std::string, bool, short, std::string>>> futures;
    
    for (const auto& [ip, hostname] : hosts) {
        futures.emplace_back(std::async(std::launch::async, [&ip, &hostname]() {
            // 使用系统ping命令
            std::string command = "timeout 5 ping -c 1 -W 3 " + ip + " > /dev/null 2>&1";
            auto start = std::chrono::high_resolution_clock::now();
            int result = system(command.c_str());
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            // 获取当前时间戳
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream timestamp;
            timestamp << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            
            // 如果命令超时，system()会返回124（timeout命令的退出码）
            // 在这种情况下，我们也认为ping失败
            bool success = (result == 0 && result != 124);
            
            return std::make_tuple(ip, hostname, success, static_cast<short>(duration), timestamp.str());
        }));
    }
    
    // 收集所有主机的结果
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults;
    
    for (auto& f : futures) {
        auto [ip, hostname, result, delay, timestamp] = f.get();
        allResults.emplace_back(ip, hostname, result, delay, timestamp);
    }
    
    return allResults;
}