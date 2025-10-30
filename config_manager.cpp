#include "config_manager.h"
#include "version_info.h"
#include <iostream>
#include <unistd.h>
#include <stdexcept>

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
        {"alerts", optional_argument, nullptr, 'a'},
        {"recovery", optional_argument, nullptr, 'r'},
        {"silent", no_argument, nullptr, 's'},
        {"cleanup", optional_argument, nullptr, 'C'},
        {"count", required_argument, nullptr, 'n'},
        {"timeout", required_argument, nullptr, 't'},
        {"version", no_argument, nullptr, 'v'},
#ifdef USE_POSTGRESQL
        {"postgresql", no_argument, nullptr, 'P'},
#endif
        {nullptr, 0, nullptr, 0}
    };
    
    // 解析命令行参数
    int opt;
    while ((opt = getopt_long(argc, argv, "hd:f:q:a::r::sC::n:t:vP", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return false;
            case 'v':
                print_version_info();
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
            case 'a':
                config.queryAlerts = -2; // 特殊值表示已启用告警但未指定天数
                // 如果提供了参数，解析天数值
                if (optarg) {
                    try {
                        config.queryAlerts = std::stoi(optarg);
                        if (config.queryAlerts < 0) {
                            std::cerr << "Alert days must be a non-negative integer." << std::endl;
                            return false;
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Invalid value for alert days: " << optarg << std::endl;
                        return false;
                    }
                }
                break;
            case 'r':
                config.queryRecoveryRecords = -2; // 特殊值表示已启用恢复记录查询但未指定天数
                // 如果提供了参数，解析天数值
                if (optarg) {
                    try {
                        config.queryRecoveryRecords = std::stoi(optarg);
                        if (config.queryRecoveryRecords < 0) {
                            std::cerr << "Recovery record days must be a non-negative integer." << std::endl;
                            return false;
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Invalid value for recovery record days: " << optarg << std::endl;
                        return false;
                    }
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
            case 'n':
                try {
                    config.pingCount = std::stoi(optarg);
                    if (config.pingCount <= 0) {
                        std::cerr << "Ping count must be a positive integer." << std::endl;
                        return false;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Invalid value for ping count: " << optarg << std::endl;
                    return false;
                }
                break;
            case 't':
                try {
                    config.timeoutSeconds = std::stoi(optarg);
                    if (config.timeoutSeconds <= 0) {
                        std::cerr << "Timeout must be a positive integer." << std::endl;
                        return false;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Invalid value for timeout: " << optarg << std::endl;
                    return false;
                }
                break;
#ifdef USE_POSTGRESQL
            case 'P':
                config.usePostgreSQL = true;
                break;
#endif
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
    std::cout << "  -v, --version\t\tShow version information\n";
    std::cout << "  -d, --database\tEnable database logging and specify database path\n";
    std::cout << "  -f, --file\t\tSpecify input file with hosts (default: ip.txt)\n";
    std::cout << "  -q, --query\t\tQuery statistics for a specific IP address (requires -d)\n";
    std::cout << "  -a, --alerts [n]\tQuery active alerts (requires -d, n: days, default: all)\n";
    std::cout << "  -r, --recovery [n]\tQuery recovery records (requires -d, n: days, default: all)\n";
    std::cout << "  -C, --cleanup [n]\tClean up data older than n days (requires -d, default: 30)\n";
    std::cout << "  -s, --silent\t\tSilent mode, suppress output\n";
    std::cout << "  -n, --count <n>\tNumber of ping packets to send (default: 3)\n";
    std::cout << "  -t, --timeout <n>\tTimeout for each ping in seconds (default: 3)\n";
#ifdef USE_POSTGRESQL
    std::cout << "  -P, --postgresql\tUse PostgreSQL database (requires -d with connection string)\n";
#endif
    std::cout << "Default behavior: If no file specified and database enabled, read hosts from database. Otherwise, read from ip.txt.\n";
    std::cout << "Default filename: ip.txt\n";
    std::cout << "Default behavior: Show all hosts with status (IP, hostname, status, delay)\n";
}