#include "config_manager.h"
#include <iostream>
#include <unistd.h>

ConfigManager::ConfigManager() {}

bool ConfigManager::parseArguments(int argc, char* argv[]) {
    // 如果没有提供任何参数，打印帮助信息并退出
    if (argc == 1) {
        printUsage(argv[0]);
        return false;
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
    while ((opt = getopt_long(argc, argv, "hd:f:q:c:sC::", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return false;
            case 'd':
                config.enableDatabase = true;
                config.databasePath = optarg;
                break;
            case 'f':
                config.filename = optarg;
                break;
            case 'q':
                config.queryIP = optarg;
                break;
            case 'c':
                // 参数现在是必需的
                try {
                    config.consecutiveFailures = std::stoi(optarg);
                } catch (const std::exception& e) {
                    std::cerr << "Invalid value for consecutive failures: " << optarg << std::endl;
                    return false;
                }
                break;
            case 's':
                config.silentMode = true;
                break;
            case 'C':
                // 如果提供了参数，使用指定的值，否则默认为30天
                config.enableDatabase = true;  // 清理功能需要启用数据库
                config.cleanupDays = (optarg) ? std::stoi(optarg) : 30;
                break;
            default:
                std::cerr << "Invalid option. Use -h or --help for usage information.\n";
                return false;
        }
    }

    
    // 如果还有剩余的参数，将其视为文件名
    if (optind < argc) {
        config.filename = argv[optind];
    }
    
    return true;
}

const ConfigManager::Config& ConfigManager::getConfig() const {
    return config;
}

void ConfigManager::printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] [filename]\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help\t\tShow this help message\n";
    std::cout << "  -d, --database\tEnable database logging and specify database path\n";
    std::cout << "  -f, --file\t\tSpecify input file with hosts (default: ip.txt)\n";
    std::cout << "  -q, --query\t\tQuery statistics for a specific IP address (requires -d)\n";
    std::cout << "  -c, --consecutive-failures <n>\tQuery hosts with n consecutive failures (requires -d)\n";
    std::cout << "  -C, --cleanup [n]\tClean up data older than n days (requires -d, default: 30)\n";
    std::cout << "  -s, --silent\t\tSilent mode, suppress output\n";
    std::cout << "Default filename: ip.txt\n";
    std::cout << "Default behavior: Show all hosts with status (IP, hostname, status, delay)\n";
}