#include "config_manager.h"

#include <unistd.h>

#include <iostream>
#include <print>
#include <stdexcept>

#include "version_info.h"

ConfigManager::ConfigManager(bool loadConfig) {
    // 根据参数决定是否加载配置文件
    config.loadConfigFile = loadConfig;
    if (config.loadConfigFile) {
        loadConfigFile();
    }
}

bool ConfigManager::loadConfigFile() {
    if (!config.configFilePath.empty()) {
        // 使用指定的配置文件路径
        if (configFile.load(config.configFilePath)) {
            applyConfigFileSettings();
            return true;
        }
        std::println(std::cerr, "Warning: Failed to load config file: {}", config.configFilePath);
        return false;
    } else {
        // 按照 XDG 规范自动查找配置文件
        if (configFile.loadFromXDGPaths()) {
            applyConfigFileSettings();
            return true;
        }
        return false;
    }
}

void ConfigManager::applyConfigFileSettings() {
    // 从配置文件中读取设置（如果存在）
    // 注意：database 开关仅由 CLI -d 选项控制，配置文件中的 database 设置仅作为 -d 未指定时的默认值
    if (configFile.has("general", "database_path")) {
        config.databasePath = configFile.get("general", "database_path", config.databasePath);
    }
    if (configFile.has("general", "silent")) {
        config.silentMode = configFile.getBool("general", "silent", config.silentMode);
    }
    if (configFile.has("general", "cleanup_days")) {
        config.cleanupDays = configFile.getInt("general", "cleanup_days", config.cleanupDays);
    }
#ifdef USE_POSTGRESQL
    if (configFile.has("general", "use_postgresql")) {
        config.usePostgreSQL =
            configFile.getBool("general", "use_postgresql", config.usePostgreSQL);
    }
#endif
}

bool ConfigManager::saveConfigFile() {
    return saveConfigFile("");
}

bool ConfigManager::saveConfigFile(const std::string& path) {
    std::string savePath = path.empty() ? configFile.getFilePath() : path;

    if (savePath.empty()) {
        // 使用默认的 XDG 配置路径
        std::string configHome = ConfigFile::getXDGConfigHome();
        if (!configHome.empty()) {
            savePath = configHome + "/mping/config";
            ConfigFile::createXDGConfigDir();
        } else {
            std::println(std::cerr, "Error: Cannot determine XDG config home directory");
            return false;
        }
    }

    // 将当前配置写入配置文件
    configFile.setBool("general", "database", config.enableDatabase);
    configFile.set("general", "database_path", config.databasePath);
    configFile.setBool("general", "silent", config.silentMode);
    configFile.setInt("general", "cleanup_days", config.cleanupDays);
#ifdef USE_POSTGRESQL
    configFile.setBool("general", "use_postgresql", config.usePostgreSQL);
#endif

    return configFile.save(savePath);
}

bool ConfigManager::parseArguments(int argc, char* argv[]) {
    // 如果没有提供任何参数，打印帮助信息并退出
    if (argc == 1) {
        printUsage(argv[0]);
        return false;
    }

    // 定义长选项
    const struct option long_options[] = {{"help", no_argument, nullptr, 'h'},
                                          {"database", required_argument, nullptr, 'd'},
                                          {"file", required_argument, nullptr, 'f'},
                                          {"query", required_argument, nullptr, 'q'},
                                          {"alerts", optional_argument, nullptr, 'a'},
                                          {"recovery", optional_argument, nullptr, 'r'},
                                          {"silent", no_argument, nullptr, 's'},
                                          {"cleanup", optional_argument, nullptr, 'C'},
                                          {"version", no_argument, nullptr, 'v'},
                                          {"config", required_argument, nullptr, 'c'},
                                          {"no-config", no_argument, nullptr, 'N'},
                                          {"save-config", optional_argument, nullptr, 'S'},
                                          {nullptr, 0, nullptr, 0}};

    // 解析命令行参数
    int opt;
    while ((opt = getopt_long(argc, argv, "hd:f:q:a::r::sC::vc:NS::", long_options, nullptr))
           != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return false;
            case 'v':
                print_version_info();
                return false;
            case 'd':
                config.enableDatabase = true;
                config.databasePath   = optarg;
                break;
            case 'f':
                config.filename = optarg;
                break;
            case 'q':
                config.queryIP = optarg;
                break;
            case 'a':
                config.queryAlerts = ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS;
                // 如果提供了参数，解析天数值
                if (optarg != nullptr) {
                    try {
                        config.queryAlerts = std::stoi(optarg);
                        if (config.queryAlerts < 0) {
                            std::println(std::cerr, "Alert days must be a non-negative integer.");
                            return false;
                        }
                    } catch (const std::exception& e) {
                        std::println(std::cerr, "Invalid value for alert days: {}", optarg);
                        return false;
                    }
                }
                break;
            case 'r':
                config.queryRecoveryRecords = ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS;
                // 如果提供了参数，解析天数值
                if (optarg != nullptr) {
                    try {
                        config.queryRecoveryRecords = std::stoi(optarg);
                        if (config.queryRecoveryRecords < 0) {
                            std::println(std::cerr,
                                         "Recovery record days must be a non-negative integer.");
                            return false;
                        }
                    } catch (const std::exception& e) {
                        std::println(std::cerr, "Invalid value for recovery record days: {}",
                                     optarg);
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
                config.cleanupDays =
                    (optarg) ? std::stoi(optarg) : ConfigDefaults::DEFAULT_CLEANUP_DAYS;
                break;
            case 'c':
                config.configFilePath = optarg;
                config.loadConfigFile = true;
                // 重新加载配置文件
                loadConfigFile();
                break;
            case 'N':
                config.loadConfigFile = false;
                break;
            case 'S':
                // 保存配置文件
                if (optarg != nullptr) {
                    if (!saveConfigFile(optarg)) {
                        std::println(std::cerr, "Failed to save config file to: {}", optarg);
                        return false;
                    }
                } else {
                    if (!saveConfigFile()) {
                        std::println(std::cerr, "Failed to save config file");
                        return false;
                    }
                }
                std::println(std::cout, "Configuration saved successfully");
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
    std::println(std::cout, "Usage: {} [options] [filename]", programName);
    std::println(std::cout, "Options:");
    std::println(std::cout, "  -h, --help\t\tShow this help message");
    std::println(std::cout, "  -v, --version\t\tShow version information");
    std::println(std::cout, "  -d, --database\tEnable database logging and specify database path");
    std::println(std::cout, "  -f, --file\t\tSpecify input file with hosts (default: ip.txt)");
    std::println(std::cout,
                 "  -q, --query\t\tQuery statistics for a specific IP address (requires -d)");
    std::println(std::cout,
                 "  -a, --alerts [n]\tQuery active alerts (requires -d, n: days, default: all)");
    std::println(std::cout,
                 "  -r, --recovery [n]\tQuery recovery records (requires -d, n: days, default: "
                 "all)");
    std::println(std::cout,
                 "  -C, --cleanup [n]\tClean up data older than n days (requires -d, default: 30)");
    std::println(std::cout, "  -s, --silent\t\tSilent mode, suppress output");
    std::println(std::cout, "  -c, --config <path>\tLoad configuration from specified file");
    std::println(std::cout, "  -N, --no-config\tDo not load configuration file");
    std::println(std::cout, "  -S, --save-config [path]\tSave current configuration to file");
    std::println(std::cout, "");
    std::println(std::cout, "Configuration File (XDG compliant):");
    std::println(std::cout, "  Default search paths (in order):");
    std::println(std::cout, "    1. $XDG_CONFIG_HOME/mping/config");
    std::println(std::cout, "    2. $XDG_CONFIG_DIRS/mping/config");
    std::println(std::cout, "    3. ~/.config/mping/config");
    std::println(std::cout, "    4. ~/.mpingrc");
    std::println(std::cout, "    5. ./mping.conf");
    std::println(std::cout, "    6. ./.mpingrc");
    std::println(std::cout, "");
    std::println(std::cout,
                 "Default behavior: If no file specified and database enabled, read hosts from "
                 "database. Otherwise, read from ip.txt.");
    std::println(std::cout, "Default filename: ip.txt");
    std::println(std::cout,
                 "Default behavior: Show all hosts with status (IP, hostname, status, delay)");
}