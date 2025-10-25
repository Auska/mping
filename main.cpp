#include "print_results.h"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <future>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
#include <iomanip>
#include <unistd.h>
#include <getopt.h>
#include <iostream>

std::map<std::string, std::string> readHostsFromFile(const std::string& filename) {
    std::map<std::string, std::string> hosts;
    std::ifstream file(filename);
    std::string line;
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return hosts;
    }
    
    while (std::getline(file, line)) {
        // 移除空行和注释行（以#开头）
        if (!line.empty() && line[0] != '#') {
            // 去除行首行尾空格
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            if (!line.empty()) {
                std::istringstream iss(line);
                std::string ip, hostname;
                if (iss >> ip >> hostname) {
                    hosts[ip] = hostname;
                }
            }
        }
    }
    
    file.close();
    return hosts;
}



int main(int argc, char* argv[]) {
    std::string filename = "ip.txt";
    bool showSuccess = false;
    
    // 定义长选项
    const struct option long_options[] = {
        {"help", no_argument, nullptr, 'h'},
        {"show-success", no_argument, nullptr, 's'},
        {nullptr, 0, nullptr, 0}
    };
    
    // 解析命令行参数
    int opt;
    while ((opt = getopt_long(argc, argv, "hs", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return 0;
            case 's':
                showSuccess = true;
                break;
            default:
                std::cerr << "Invalid option. Use -h or --help for usage information.\n";
                return 1;
        }
    }
    
    // 如果还有剩余的参数，将其视为文件名
    if (optind < argc) {
        filename = argv[optind];
    }
    
    std::map<std::string, std::string> hosts = readHostsFromFile(filename);
    
    if (hosts.empty()) {
        std::cerr << "No hosts to ping. Please check the input file." << std::endl;
        return 1;
    }
    
    // 使用C++20的特性来并发执行ping操作
    std::vector<std::future<std::tuple<std::string, std::string, bool, long long>>> futures;
    
    for (const auto& [ip, hostname] : hosts) {
        futures.emplace_back(std::async(std::launch::async, [&ip, &hostname]() {
            // 使用系统ping命令
            std::string command = "timeout 5 ping -c 1 -W 3 " + ip + " > /dev/null 2>&1";
            auto start = std::chrono::high_resolution_clock::now();
            int result = system(command.c_str());
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            // 如果命令超时，system()会返回124（timeout命令的退出码）
            // 在这种情况下，我们也认为ping失败
            bool success = (result == 0 && result != 124);
            
            return std::make_tuple(ip, hostname, success, static_cast<long long>(duration));
        }));
    }
    
    // 收集成功和失败的主机
    std::vector<std::pair<std::string, std::string>> failedHosts;
    std::vector<std::tuple<std::string, std::string, long long>> successfulHosts;
    
    // 等待所有任务完成并收集结果
    for (auto& f : futures) {
        auto [ip, hostname, result, delay] = f.get();
        if (!result) {
            failedHosts.emplace_back(ip, hostname);
        } else if (showSuccess) {
            successfulHosts.emplace_back(ip, hostname, delay);
        }
    }
    
    // 输出成功的主机（如果需要）
    if (showSuccess) {
        printSuccessfulHosts(successfulHosts);
    } else {
    // 默认输出失败的主机
    printFailedHosts(failedHosts);
    }

    return 0;
}
