#include "ping_manager.h"
#include <iostream>
#include <algorithm>
#include <vector>

std::vector<std::tuple<std::string, std::string, bool, short, std::string>> PingManager::performPing(const std::map<std::string, std::string>& hosts, int pingCount) {
    // 使用C++20的特性来并发执行ping操作
    std::vector<std::future<std::tuple<std::string, std::string, bool, short, std::string>>> futures;
    
    for (const auto& [ip, hostname] : hosts) {
        futures.emplace_back(std::async(std::launch::async, [&ip, &hostname, pingCount]() {
            // 发送指定数量的包并记录每次的延迟
            std::vector<short> delays;
            bool success = false;
            
            for (int i = 0; i < pingCount; ++i) {
                std::string command = "timeout 5 ping -c 1 -W 3 " + ip + " > /dev/null 2>&1";
                auto start = std::chrono::high_resolution_clock::now();
                int result = system(command.c_str());
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                
                // 如果任何一次成功，则标记为成功
                if (result == 0) {
                    success = true;
                }
                
                // 记录延迟（即使失败也记录）
                delays.push_back(duration);
            }
            
            // 取所有延迟中的最小值
            short minDelay = *std::min_element(delays.begin(), delays.end());
            
            // 获取当前时间戳
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream timestamp;
            timestamp << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            
            return std::make_tuple(ip, hostname, success, static_cast<short>(minDelay), timestamp.str());
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
