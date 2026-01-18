#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <getopt.h>

#include <map>
#include <string>

#include "config_file.h"

class ConfigManager {
   public:
    struct Config {
        std::string filename       = "";
        bool enableDatabase        = false;
        std::string databasePath   = "ping_monitor.db";
        bool silentMode            = false;
        std::string queryIP        = "";
        int cleanupDays            = -1;    // -1表示不执行清理
        int queryAlerts            = -1;    // -1表示不查询告警，>=0表示查询指定天数内的告警
        int queryRecoveryRecords   = -1;    // -1表示不查询恢复记录，>=0表示查询指定天数内的恢复记录
        int pingCount              = 3;     // 默认发送3个包
        int timeoutSeconds         = 3;     // 默认超时时间（秒）
        bool loadConfigFile        = true;  // 是否加载配置文件
        std::string configFilePath = "";    // 指定的配置文件路径
#ifdef USE_POSTGRESQL
        bool usePostgreSQL = false;  // 是否使用PostgreSQL数据库
#endif
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