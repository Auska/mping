#include "print_results.h"
#include "db.h"
#include "utils.h"
#include "ping.h"
#include <vector>
#include <string>
#include <map>
#include <unistd.h>
#include <getopt.h>
#include <iostream>



int main(int argc, char* argv[]) {
    std::string filename = "ip.txt";
    bool enableDatabase = false;  // 默认不启用数据库
    std::string databasePath = "ping_monitor.db";  // 默认数据库路径
    bool silentMode = false;  // 默认不启用静默模式
    
    // 如果没有提供任何参数，打印帮助信息并退出
    if (argc == 1) {
        printUsage(argv[0]);
        return 0;
    }
    
    // 定义长选项
    const struct option long_options[] = {
        {"help", no_argument, nullptr, 'h'},
        {"database", required_argument, nullptr, 'd'},
        {"file", required_argument, nullptr, 'f'},
        {"query", required_argument, nullptr, 'q'},
        {"consecutive-failures", required_argument, nullptr, 'c'},
        {"silent", no_argument, nullptr, 's'},
        {"cleanup", optional_argument, nullptr, 'C'},
        {nullptr, 0, nullptr, 0}
    };
    
    // 解析命令行参数
    int opt;
    std::string queryIP = "";
    int consecutiveFailures = -1;  // -1表示不查询连续失败
    int cleanupDays = -1;  // -1表示不执行清理
    while ((opt = getopt_long(argc, argv, "hd:f:q:c:sC::", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return 0;
            case 'd':
                enableDatabase = true;
                databasePath = optarg;
                break;
            case 'f':
                filename = optarg;
                break;
            case 'q':
                queryIP = optarg;
                break;
            case 'c':
                // 参数现在是必需的
                try {
                    consecutiveFailures = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Invalid value for consecutive failures: " << optarg << std::endl;
                    return 1;
                }
                break;
            case 's':
                silentMode = true;
                break;
            case 'C':
                // 如果提供了参数，使用指定的值，否则默认为30天
                enableDatabase = true;  // 清理功能需要启用数据库
                cleanupDays = (optarg) ? std::stoi(optarg) : 30;
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
        if (!enableDatabase) {
            std::cerr << "Database must be enabled to query statistics. Use -d option to specify database path.\n";
            return 1;
        }
        
        Database db(databasePath);
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.queryIPStatistics(queryIP);
        return 0;
    }
    
    // 如果请求执行清理操作
    if (cleanupDays >= 0) {
        if (!enableDatabase) {
            std::cerr << "Database must be enabled to cleanup data. Use -d option to specify database path.\n";
            return 1;
        }
        
        Database db(databasePath);
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.cleanupOldData(cleanupDays);
        return 0;
    }
    
    // 如果请求查询连续失败的主机，则只显示查询结果，不执行ping操作
    if (consecutiveFailures >= 0) {
        if (!enableDatabase) {
            std::cerr << "Database must be enabled to query consecutive failures. Use -d option to specify database path.\n";
            return 1;
        }
        
        Database db(databasePath);
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
    
    // 执行ping操作
    std::vector<std::tuple<std::string, std::string, bool, long long, std::string>> allResults = performPing(hosts);
    
    // 初始化数据库
    Database db(databasePath);
    if (enableDatabase) {
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        // 将结果插入数据库
        for (const auto& [ip, hostname, result, delay, timestamp] : allResults) {
            db.insertPingResult(ip, hostname, delay, result, timestamp);
        }
    }
    
    // 打印所有IP地址和结果（除非启用静默模式）
    if (!silentMode) {
        for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
            std::cout << ip << "\t" << hostname << "\t" << (success ? "success" : "failed") << "\t" << delay << "ms" << std::endl;
        }
    }

    return 0;
}
