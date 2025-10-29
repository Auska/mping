#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <getopt.h>
#include "project_info.h"

class ConfigManager {
public:
    struct Config {
        std::string filename = "";
        bool enableDatabase = false;
        std::string databasePath = "ping_monitor.db";
        bool silentMode = false;
        std::string queryIP = "";
        int consecutiveFailures = -1;  // -1表示不查询连续失败
        int cleanupDays = -1;  // -1表示不执行清理
        bool queryAlerts = false;  // 是否查询告警
        int pingCount = 3;  // 默认发送3个包
        int timeoutSeconds = 3;  // 默认超时时间（秒）
#ifdef USE_POSTGRESQL
        bool usePostgreSQL = false;  // 是否使用PostgreSQL数据库
#endif
    };

private:
    Config config;

public:
    ConfigManager();
    
    // 解析命令行参数
    bool parseArguments(int argc, char* argv[]);
    
    // 获取配置
    const Config& getConfig() const;
    
    // 打印使用帮助
    void printUsage(const char* programName);
};

#endif // CONFIG_MANAGER_H