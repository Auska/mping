#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <getopt.h>

class ConfigManager {
public:
    struct Config {
        std::string filename = "ip.txt";
        bool enableDatabase = false;
        std::string databasePath = "ping_monitor.db";
        bool silentMode = false;
        std::string queryIP = "";
        int consecutiveFailures = -1;  // -1表示不查询连续失败
        int cleanupDays = -1;  // -1表示不执行清理
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