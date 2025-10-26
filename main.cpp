#include "config_manager.h"
#include "database_manager.h"
#include "ping_manager.h"
#include "utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <map>

int main(int argc, char* argv[]) {
    // 创建配置管理器并解析命令行参数
    ConfigManager configManager;
    if (!configManager.parseArguments(argc, argv)) {
        return 0;
    }
    
    const ConfigManager::Config& config = configManager.getConfig();
    
    // 如果提供了查询IP，则只显示查询结果，不执行ping操作
    if (!config.queryIP.empty()) {
        if (!config.enableDatabase) {
            std::cerr << "Database must be enabled to query statistics. Use -d option to specify database path.\n";
            return 1;
        }
        
        DatabaseManager db(config.databasePath);
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.queryIPStatistics(config.queryIP);
        return 0;
    }
    
    // 如果请求执行清理操作
    if (config.cleanupDays >= 0) {
        if (!config.enableDatabase) {
            std::cerr << "Database must be enabled to cleanup data. Use -d option to specify database path.\n";
            return 1;
        }
        
        DatabaseManager db(config.databasePath);
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.cleanupOldData(config.cleanupDays);
        return 0;
    }
    
    // 如果请求查询连续失败的主机，则只显示查询结果，不执行ping操作
    if (config.consecutiveFailures >= 0) {
        if (!config.enableDatabase) {
            std::cerr << "Database must be enabled to query consecutive failures. Use -d option to specify database path.\n";
            return 1;
        }
        
        DatabaseManager db(config.databasePath);
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        db.queryConsecutiveFailures(config.consecutiveFailures);
        return 0;
    }
    
    // 读取主机列表
    std::map<std::string, std::string> hosts = readHostsFromFile(config.filename);
    
    if (hosts.empty()) {
        std::cerr << "No hosts to ping. Please check the input file." << std::endl;
        return 1;
    }
    
    // 创建ping管理器并执行ping操作
    PingManager pingManager;
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults = pingManager.performPing(hosts, config.pingCount);
    
    // 初始化数据库管理器
    DatabaseManager db(config.databasePath);
    if (config.enableDatabase) {
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
    if (!config.silentMode) {
        for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
            std::cout << ip << "\t" << hostname << "\t" << (success ? "success" : "failed") << "\t" << delay << "ms" << std::endl;
        }
    }

    return 0;
}
