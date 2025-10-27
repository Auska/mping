#include "config_manager.h"
#include "database_manager.h"
#include "ping_manager.h"
#include "utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <exception>

int main(int argc, char* argv[]) {
    try {
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
        std::map<std::string, std::string> hosts;
        
        // 如果指定了文件名（通过-f参数或命令行参数），则从文件读取主机列表
        // 否则如果启用了数据库，则从数据库的hosts表读取主机列表
        if (!config.filename.empty()) {
            hosts = readHostsFromFile(config.filename);
        } else if (config.enableDatabase) {
            DatabaseManager db(config.databasePath);
            if (!db.initialize()) {
                std::cerr << "Failed to initialize database" << std::endl;
                return 1;
            }
            hosts = db.getAllHosts();
        }
        
        if (hosts.empty()) {
            std::cerr << "No hosts to ping. Please check the input file or database." << std::endl;
            return 1;
        }
        
        // 创建ping管理器并执行ping操作
        PingManager pingManager;
        // 使用默认最大并发数执行ping操作
        std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults = 
            pingManager.performPing(hosts, config.pingCount, config.timeoutSeconds);
        
        // 如果启用了数据库，则初始化数据库管理器并存储结果
        if (config.enableDatabase) {
            DatabaseManager db(config.databasePath);
            if (!db.initialize()) {
                std::cerr << "Failed to initialize database" << std::endl;
                return 1;
            }
            
            // 将结果转换为数据库所需的格式并批量插入
            std::vector<std::tuple<std::string, std::string, short, bool, std::string>> dbResults;
            dbResults.reserve(allResults.size());
            
            for (const auto& [ip, hostname, result, delay, timestamp] : allResults) {
                dbResults.emplace_back(ip, hostname, delay, result, timestamp);
            }
            
            if (!db.insertPingResults(dbResults)) {
                std::cerr << "Failed to insert ping results into database" << std::endl;
                return 1;
            }
        }
        
        // 打印所有IP地址和结果（除非启用静默模式）
        if (!config.silentMode) {
            for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
                std::cout << ip << "\t" << hostname << "\t" << (success ? "success" : "failed") << "\t" << delay << "ms" << std::endl;
            }
        }
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception occurred" << std::endl;
        return 1;
    }
}
