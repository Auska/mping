#include "config_manager.h"

#include <unistd.h>

#include <cctype>
#include <iostream>
#include <print>
#include <stdexcept>

#include "version_info.h"

namespace {

// 判断字符串是否为非负整数（-C/-a/-r 空格分隔天数形式的识别）
bool isNonNegativeInteger(const char* s) {
    if (s == nullptr || *s == '\0') {
        return false;
    }
    for (const char* p = s; *p != '\0'; p++) {
        if (!std::isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
    }
    return true;
}

// 解析可选的 -a/-r/-C 天数参数；optarg 为空时使用 defaultDays（哨兵值或默认值）
bool parseOptionalDays(const char* optarg, const char* optionName, int& value, int defaultDays) {
    if (optarg == nullptr) {
        value = defaultDays;
        return true;
    }
    try {
        value = std::stoi(optarg);
        if (value < 0) {
            std::println(std::cerr, "{} days must be a non-negative integer.", optionName);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::println(std::cerr, "Invalid value for {} days: {}", optionName, optarg);
        return false;
    }
}

}  // namespace

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
    // 注意：database 开关仅由 CLI -d 选项控制，配置文件不读取 database 键
    if (configFile.has("general", "database_path")) {
        config.databasePath = configFile.get("general", "database_path", config.databasePath);
    }
    if (configFile.has("general", "silent")) {
        config.silentMode = configFile.getBool("general", "silent", config.silentMode);
    }
    if (configFile.has("general", "cleanup_days")) {
        config.cleanupDays = configFile.getInt("general", "cleanup_days", config.cleanupDays);
    }
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

    // 将当前配置写入配置文件（database 开关仅由 CLI -d 控制，不落盘）
    configFile.set("general", "database_path", config.databasePath);
    configFile.setBool("general", "silent", config.silentMode);
    configFile.setInt("general", "cleanup_days", config.cleanupDays);

    return configFile.save(savePath);
}

bool ConfigManager::parseArguments(int argc, char* argv[], int* exitCode) {
    // 如果没有提供任何参数，打印帮助信息并退出
    if (argc == 1) {
        printUsage(argv[0]);
        return false;
    }

    // 参数错误时写入 1，正常退出（-h/-v/-S）保持 0
    auto failWithError = [&] {
        if (exitCode) {
            *exitCode = 1;
        }
        return false;
    };

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
    // optional_argument 的空格分隔形式（如 "-C 30"）不会被 getopt 消费：
    // 记录哪些选项处于"已启用但未带参"状态，稍后消费数字位置参数作为天数
    bool cleanupBare  = false;
    bool alertsBare   = false;
    bool recoveryBare = false;
    while ((opt = getopt_long(argc, argv, "hd:f:q:a::r::sC::vc:NS::", long_options, nullptr))
           != -1) {
        switch (opt) {
            case '?':
                // 未知选项或缺必需参数：getopt 已打印原因，给出提示并以错误码退出
                std::println(std::cerr, "Try '{} --help' for more information.", argv[0]);
                return failWithError();
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
                alertsBare = (optarg == nullptr);
                if (!parseOptionalDays(optarg, "Alert", config.queryAlerts,
                                       ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS)) {
                    return failWithError();
                }
                break;
            case 'r':
                recoveryBare = (optarg == nullptr);
                if (!parseOptionalDays(optarg, "Recovery record", config.queryRecoveryRecords,
                                       ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS)) {
                    return failWithError();
                }
                break;
            case 's':
                config.silentMode = true;
                break;
            case 'C':
                config.enableDatabase = true;  // 清理功能需要启用数据库
                cleanupBare           = (optarg == nullptr);
                if (!parseOptionalDays(optarg, "Cleanup", config.cleanupDays,
                                       ConfigDefaults::DEFAULT_CLEANUP_DAYS)) {
                    return failWithError();
                }
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
                // 保存配置文件；optional_argument 不吃空格分隔路径，这里手动消费
                if (optarg != nullptr) {
                    if (!saveConfigFile(optarg)) {
                        std::println(std::cerr, "Failed to save config file to: {}", optarg);
                        return false;
                    }
                } else {
                    const char* savePath = "";
                    if (optind < argc && argv[optind][0] != '-') {
                        savePath = argv[optind];
                        optind++;  // 消费空格分隔的保存路径
                    }
                    if (!saveConfigFile(savePath)) {
                        std::println(std::cerr, "Failed to save config file");
                        return false;
                    }
                }
                std::println(std::cout, "Configuration saved successfully");
                return false;
        }
    }

    // 空格分隔的天数形式（文档 "-C [n]"）：`-C 30` 中 "30" 按 getopt 规则成为位置参数，
    // 若它是非负整数且对应选项未带参，则将其消费为天数；否则保持文件名语义
    bool consumedAsDays = false;
    if (optind < argc && isNonNegativeInteger(argv[optind])) {
        try {
            const int days = std::stoi(argv[optind]);
            if (cleanupBare) {
                config.cleanupDays = days;
            } else if (alertsBare) {
                config.queryAlerts = days;
            } else if (recoveryBare) {
                config.queryRecoveryRecords = days;
            }
            consumedAsDays = cleanupBare || alertsBare || recoveryBare;
        } catch (const std::exception&) {
            // 数字超出 int 范围等：按文件名处理
        }
    }
    if (consumedAsDays) {
        optind++;
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