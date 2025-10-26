#include "print_results.h"
#include "db.h"
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
    bool enableDatabase = true;  // 默认启用数据库
    
    // 定义长选项
    const struct option long_options[] = {
        {"help", no_argument, nullptr, 'h'},
        {"database", no_argument, nullptr, 'd'},
        {"file", required_argument, nullptr, 'f'},
        {"query", required_argument, nullptr, 'q'},
        {"consecutive-failures", optional_argument, nullptr, 'c'},
        {nullptr, 0, nullptr, 0}
    };
    
    // 解析命令行参数
    int opt;
    std::string queryIP = "";
    int consecutiveFailures = -1;  // -1表示不查询连续失败
    while ((opt = getopt_long(argc, argv, "hdf:q:c::", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return 0;
            case 'd':
                enableDatabase = true;
                break;
            case 'f':
                filename = optarg;
                break;
            case 'q':
                queryIP = optarg;
                break;
            case 'c':
                // 如果提供了参数，使用指定的值，否则默认为3
                consecutiveFailures = (optarg) ? std::stoi(optarg) : 3;
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
    
    // 如果提供了查询IP，则只显示查询结果，不执行ping操作
    if (!queryIP.empty()) {
        Database db("ping_monitor.db");
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.queryIPStatistics(queryIP);
        return 0;
    }
    
    // 如果请求查询连续失败的主机，则只显示查询结果，不执行ping操作
    if (consecutiveFailures >= 0) {
        Database db("ping_monitor.db");
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.queryConsecutiveFailures(consecutiveFailures);
        return 0;
    }
    
    std::map<std::string, std::string> hosts = readHostsFromFile(filename);
    
    if (hosts.empty()) {
        std::cerr << "No hosts to ping. Please check the input file." << std::endl;
        return 1;
    }
    
    // 使用C++20的特性来并发执行ping操作
    std::vector<std::future<std::tuple<std::string, std::string, bool, long long, std::string>>> futures;  // 添加时间戳
    
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
            
            return std::make_tuple(ip, hostname, success, static_cast<long long>(duration), timestamp.str());
        }));
    }
    
    // 收集所有主机的结果
    std::vector<std::tuple<std::string, std::string, bool, long long, std::string>> allResults;  // 添加时间戳
    
    // 等待所有任务完成并收集结果
    // 初始化数据库
    Database db("ping_monitor.db");
    if (enableDatabase) {
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
    }
    
    for (auto& f : futures) {
        auto [ip, hostname, result, delay, timestamp] = f.get();
        // 将结果插入数据库
        if (enableDatabase) {
            db.insertPingResult(ip, hostname, delay, result, timestamp);
        }
        
        // 保存所有结果
        allResults.emplace_back(ip, hostname, result, delay, timestamp);
    }
    
    // 打印所有IP地址和结果
    for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
        std::cout << ip << "\t" << hostname << "\t" << (success ? "success" : "failed") << "\t" << delay << "ms" << std::endl;
    }

    return 0;
}
