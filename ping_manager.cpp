#include "ping_manager.h"
#include <iostream>
#include <print>
#include <algorithm>
#include <vector>
#include <atomic>
#include <stdexcept>
#include <ranges>

// Ping工作函数
std::tuple<std::string, std::string, bool, short, std::string> pingHost(const std::string& ip, const std::string& hostname, int pingCount, int timeoutSeconds) {
    // 发送指定数量的包并记录每次的延迟
    std::vector<short> delays;
    bool success = false;
    
    for (int i = 0; i < pingCount; ++i) {
        // 使用配置的超时时间
        std::string command = std::format("timeout {} ping -c 1 -W {} {} > /dev/null 2>&1", 
                                         timeoutSeconds + 2, timeoutSeconds, ip);
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
    short minDelay = *std::ranges::min_element(delays);
    
    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    
    return std::make_tuple(ip, hostname, success, static_cast<short>(minDelay), timestamp.str());
}

std::vector<std::tuple<std::string, std::string, bool, short, std::string>> PingManager::performPing(
    const std::map<std::string, std::string>& hosts, 
    int pingCount, 
    int timeoutSeconds,
    size_t maxConcurrent) {
    
    // 如果主机数量小于等于最大并发数，直接并发执行所有ping操作
    if (hosts.size() <= maxConcurrent) {
        std::vector<std::future<std::tuple<std::string, std::string, bool, short, std::string>>> futures;
        
        for (const auto& [ip, hostname] : hosts) {
            futures.emplace_back(std::async(std::launch::async, [ip, hostname, pingCount, timeoutSeconds]() {
                return pingHost(ip, hostname, pingCount, timeoutSeconds);
            }));
        }
        
        // 收集所有主机的结果
        std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults;
        allResults.reserve(hosts.size());
        
        for (auto& f : futures) {
            allResults.push_back(f.get());
        }
        
        return allResults;
    }
    
    // 如果主机数量大于最大并发数，分批执行ping操作
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults;
    allResults.reserve(hosts.size());
    
    // 创建一个互斥锁来保护结果容器
    std::mutex resultsMutex;
    
    // 将所有主机放入队列
    std::queue<std::pair<std::string, std::string>> hostQueue;
    for (const auto& [ip, hostname] : hosts) {
        hostQueue.emplace(ip, hostname);
    }
    
    // 创建工作线程池
    std::vector<std::thread> workers;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop(false);
    
    // 启动工作线程
    for (size_t i = 0; i < maxConcurrent; ++i) {
        workers.emplace_back([&, timeoutSeconds]() {
            while (true) {
                std::pair<std::string, std::string> host;
                
                // 从队列中获取主机
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [&] { return stop.load() || !hostQueue.empty(); });
                    
                    if (stop.load() && hostQueue.empty()) {
                        return;
                    }
                    
                    if (!hostQueue.empty()) {
                        host = hostQueue.front();
                        hostQueue.pop();
                    } else {
                        continue;
                    }
                }
                
                // 执行ping操作
                auto result = pingHost(host.first, host.second, pingCount, timeoutSeconds);
                
                // 将结果添加到结果容器中
                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    allResults.push_back(result);
                }
            }
        });
    }
    
    // 等待所有主机都被处理
    while (true) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (hostQueue.empty()) {
                stop.store(true);
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 通知所有工作线程停止
    condition.notify_all();
    
    // 等待所有工作线程完成
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    return allResults;
}
