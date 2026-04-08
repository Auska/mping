#include "ping_manager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <print>
#include <ranges>
#include <stdexcept>
#include <vector>

// Ping工作函数 - 优化版，使用fork和execvp替代system调用以提高性能
std::tuple<std::string, std::string, bool, short, std::string> pingHost(const std::string& ip,
                                                                        const std::string& hostname,
                                                                        int pingCount,
                                                                        int timeoutSeconds) {
    // 发送指定数量的包并记录每次的延迟
    std::vector<short> delays;
    bool success = false;

    for (int i = 0; i < pingCount; ++i) {
        // 使用fork和execvp替代system调用，更高效
        pid_t pid = fork();

        if (pid == 0) {
            // 子进程 - 重定向stdout和stderr到/dev/null以实现静默模式
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);

            // 执行ping命令
            execlp("ping", "ping", "-c", "1", "-W", std::to_string(timeoutSeconds).c_str(),
                   ip.c_str(), (char*)NULL);
            // 如果execlp失败，退出子进程
            exit(1);
        } else if (pid > 0) {
            // 父进程 - 等待子进程完成
            auto start = std::chrono::high_resolution_clock::now();
            int status;
            waitpid(pid, &status, 0);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            // 检查ping命令是否成功
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                success = true;
            }

            // 记录延迟（即使失败也记录）
            delays.push_back(static_cast<short>(duration));
        } else {
            // fork失败
            std::cerr << "Failed to fork process for ping: " << ip << std::endl;
            delays.push_back(static_cast<short>(timeoutSeconds * 1000));  // 超时值作为延迟
        }
    }

    // 取所有延迟中的最小值
    short minDelay = *std::ranges::min_element(delays);

    // 获取当前时间戳（使用UTC时间）
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");

    return std::make_tuple(ip, hostname, success, minDelay, timestamp.str());
}

std::vector<std::tuple<std::string, std::string, bool, short, std::string>>
PingManager::performPing(const std::map<std::string, std::string>& hosts, int pingCount,
                         int timeoutSeconds, size_t maxConcurrent) {
    // 如果主机数量小于等于最大并发数，直接并发执行所有ping操作
    if (hosts.size() <= maxConcurrent) {
        std::vector<std::future<std::tuple<std::string, std::string, bool, short, std::string>>>
            futures;

        for (const auto& [ip, hostname] : hosts) {
            futures.emplace_back(
                std::async(std::launch::async, pingHost, ip, hostname, pingCount, timeoutSeconds));
        }

        // 收集所有主机的结果
        std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults;
        allResults.reserve(hosts.size());

        for (auto& f : futures) {
            allResults.push_back(f.get());
        }

        return allResults;
    }

    // 如果主机数量大于最大并发数，使用线程池优化实现
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
    std::vector<std::jthread> workers;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop(false);

    // 添加完成计数器和条件变量，避免忙等待
    std::atomic<size_t> activeTasks(hosts.size());
    std::mutex completionMutex;
    std::condition_variable completionCV;

    // 启动工作线程
    for (size_t i = 0; i < maxConcurrent; ++i) {
        workers.emplace_back([&, pingCount, timeoutSeconds]() {
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

                // 减少活动任务计数，如果所有任务完成则通知主线程
                if (--activeTasks == 0) {
                    completionCV.notify_one();
                }
            }
        });
    }

    // 等待所有任务完成（使用条件变量，避免忙等待）
    {
        std::unique_lock<std::mutex> lock(completionMutex);
        completionCV.wait(lock, [&] { return activeTasks.load() == 0; });
    }

    // 通知所有工作线程停止
    stop.store(true);
    condition.notify_all();

    // std::jthread 会自动 join，无需手动调用

    return allResults;
}
