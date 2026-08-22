#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <getopt.h>

#include <string>

#include "config_file.h"
#include "constants.h"

class ConfigManager {
   public:
    struct Config {
        std::string filename     = "";
        bool enableDatabase      = false;
        std::string databasePath = "host=localhost user=postgres dbname=mping_pgtest";
        bool silentMode          = false;
        std::string queryIP      = "";
        int cleanupDays          = -1;  // -1表示不执行清理
        int queryAlerts = -1;  // -1=不查询, >=0=查询指定天数, QUERY_MODE_ENABLED_NO_DAYS=查询所有
        int queryRecoveryRecords =
            -1;  // -1=不查询, >=0=查询指定天数, QUERY_MODE_ENABLED_NO_DAYS=查询所有
        bool loadConfigFile        = true;  // 是否加载配置文件
        std::string configFilePath = "";    // 指定的配置文件路径
    };

   private:
    Config config;
    ConfigFile configFile;

    // 从配置文件加载配置
    bool loadConfigFile();

    // 应用配置文件中的设置
    void applyConfigFileSettings();

   public:
    ConfigManager(bool loadConfig = true);

    // 解析命令行参数
    bool parseArguments(int argc, char* argv[]);

    // 获取配置
    const Config& getConfig() const;

    // 打印使用帮助
    void printUsage(const char* programName);

    // 保存当前配置到默认配置文件
    bool saveConfigFile();

    // 保存当前配置到指定路径
    bool saveConfigFile(const std::string& path);
};

#endif  // CONFIG_MANAGER_H