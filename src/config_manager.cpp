#include "config_manager.h"

#include <unistd.h>

#include <cctype>
#include <iostream>
#include <print>
#include <stdexcept>

#include "version_info.h"

namespace {

// 解析可选的 -a/-r/-C 天数参数（仅附加形式，如 -C90）；optarg 为空时使用 defaultDays
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

ConfigManager::ConfigManager() {
    // 数据库通过配置文件 database_path 配置；优先使用 -c 指定路径（解析时重新加载），
    // 否则加载默认路径（$HOME/.config/mping/config.ini）
    loadConfigFile();
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
        // 使用默认配置文件路径
        const std::string defaultPath = ConfigFile::getDefaultConfigPath();
        if (!defaultPath.empty() && configFile.load(defaultPath)) {
            applyConfigFileSettings();
            return true;
        }
        return false;
    }
}

void ConfigManager::applyConfigFileSettings() {
    // 从配置文件中读取设置（如果存在）；database_path 存在即启用数据库
    if (configFile.has("general", "database_path")) {
        config.databasePath    = configFile.get("general", "database_path", config.databasePath);
        config.databasePathSet = true;
    }
    if (configFile.has("general", "silent")) {
        config.silentMode = configFile.getBool("general", "silent", config.silentMode);
    }
    if (configFile.has("general", "cleanup_days")) {
        config.cleanupDays = configFile.getInt("general", "cleanup_days", config.cleanupDays);
    }
    // 持续检查模式：>0 = 每轮间隔秒数；0/缺失 = 单次运行；负数视为配置错误
    if (configFile.has("general", "check_interval")) {
        config.checkIntervalSeconds =
            configFile.getInt("general", "check_interval", config.checkIntervalSeconds);
        if (config.checkIntervalSeconds < 0) {
            std::println(std::cerr,
                         "Warning: check_interval must be a non-negative integer, ignored.");
            config.checkIntervalSeconds = 0;
        }
    }
}

bool ConfigManager::saveConfigFile() {
    return saveConfigFile("");
}

bool ConfigManager::saveConfigFile(const std::string& path) {
    std::string savePath = path.empty() ? configFile.getFilePath() : path;

    if (savePath.empty()) {
        // 使用默认的配置文件路径
        savePath = ConfigFile::getDefaultConfigPath();
        if (savePath.empty()) {
            std::println(std::cerr, "Error: Cannot determine home directory");
            return false;
        }
    }

    // 将当前配置写入配置文件
    configFile.set("general", "database_path", config.databasePath);
    configFile.setBool("general", "silent", config.silentMode);
    configFile.setInt("general", "cleanup_days", config.cleanupDays);
    configFile.setInt("general", "check_interval", config.checkIntervalSeconds);

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

    // 解析命令行参数（仅短选项；-a/-r/-C 的天数仅支持附加形式，如 -C90）
    int opt;
    while ((opt = getopt(argc, argv, "hf:q:a::r::sC::vc:S::")) != -1) {
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
            case 'f':
                config.filename = optarg;
                break;
            case 'q':
                config.queryIP = optarg;
                break;
            case 'a':
                if (!parseOptionalDays(optarg, "Alert", config.queryAlerts,
                                       ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS)) {
                    return failWithError();
                }
                break;
            case 'r':
                if (!parseOptionalDays(optarg, "Recovery record", config.queryRecoveryRecords,
                                       ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS)) {
                    return failWithError();
                }
                break;
            case 's':
                config.silentMode = true;
                break;
            case 'C':
                if (!parseOptionalDays(optarg, "Cleanup", config.cleanupDays,
                                       ConfigDefaults::DEFAULT_CLEANUP_DAYS)) {
                    return failWithError();
                }
                break;
            case 'c':
                config.configFilePath = optarg;
                // 重新加载配置文件
                loadConfigFile();
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
    std::println(std::cout, "Options (short only):");
    std::println(std::cout, "  -h\t\tShow this help message");
    std::println(std::cout, "  -v\t\tShow version information");
    std::println(std::cout, "  -f <file>\tSpecify input file with hosts (default: ip.txt)");
    std::println(std::cout, "  -q <ip>\tQuery statistics for a specific IP address");
    std::println(std::cout, "  -a [n]\t\tQuery active alerts (n: days, default: all)");
    std::println(std::cout, "  -r [n]\t\tQuery recovery records (n: days, default: all)");
    std::println(std::cout, "  -C [n]\t\tClean up data older than n days (default: 30)");
    std::println(std::cout, "  -s\t\tSilent mode, suppress output");
    std::println(std::cout, "  -c <path>\tLoad configuration from specified file");
    std::println(std::cout, "  -S [path]\tSave current configuration to file");
    std::println(std::cout, "");
    std::println(std::cout, "Note: -a/-r/-C days attach directly (e.g. -a7).");
    std::println(std::cout, "");
    std::println(std::cout, "Configuration File:");
    std::println(std::cout, "  Default path: $HOME/.config/mping/config.ini");
    std::println(std::cout, "  [general] keys: database_path, silent, cleanup_days,");
    std::println(std::cout,
                 "  check_interval (continuous mode: seconds between rounds, 0/absent = single "
                 "run)");
    std::println(std::cout, "");
    std::println(std::cout,
                 "Database is configured only via database_path in the config file; -q/-a/-r/-C");
    std::println(std::cout, "  and host loading from DB require it.");
    std::println(std::cout,
                 "Default behavior: If no file specified and database_path set, read hosts from ");
    std::println(std::cout, "  database. Otherwise, read from ip.txt.");
    std::println(std::cout, "Default filename: ip.txt");
    std::println(std::cout,
                 "Default behavior: Show all hosts with status (IP, hostname, status, delay)");
}