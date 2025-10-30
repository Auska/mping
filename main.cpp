#include "config_manager.h"
#include "database_manager.h"
#ifdef USE_POSTGRESQL
#include "database_manager_pg.h"
#endif
#include "ping_manager.h"
#include "utils.h"
#include <iostream>
#include <print>
#include <vector>
#include <string>
#include <map>
#include <exception>

// 模板函数：处理数据库操作的通用模式
template<typename DatabaseType>
bool initializeDatabase(const std::string& databasePath, DatabaseType& db) {
    if (!db.initialize()) {
        std::println(std::cerr, "Failed to initialize database");
        return false;
    }
    return true;
}

// 模板函数：查询IP统计信息
template<typename DatabaseType>
void queryIPStatistics(const std::string& databasePath, const std::string& queryIP) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return;
    }
    db.queryIPStatistics(queryIP);
}

// 模板函数：清理旧数据
template<typename DatabaseType>
void cleanupOldData(const std::string& databasePath, int cleanupDays) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return;
    }
    db.cleanupOldData(cleanupDays);
}

// 模板函数：查询活动告警
template<typename DatabaseType>
void queryActiveAlerts(const std::string& databasePath, int queryAlerts) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return;
    }
    
    auto alerts = db.getActiveAlerts(queryAlerts);
    if (alerts.empty()) {
        if (queryAlerts >= 0) {
            std::println(std::cout, "No active alerts within the last {} days.", queryAlerts);
        } else {
            std::println(std::cout, "No active alerts.");
        }
    } else {
        if (queryAlerts >= 0) {
            std::println(std::cout, "Active alerts within the last {} days:", queryAlerts);
        } else {
            std::println(std::cout, "Active alerts:");
        }
        std::println(std::cout, "IP Address\tHostname\tCreated Time");
        std::println(std::cout, "------------------------------------------------");
        for (const auto& [ip, hostname, createdTime] : alerts) {
            std::println(std::cout, "{}\t{}\t{}", ip, hostname, createdTime);
        }
    }
}

// 模板函数：查询恢复记录
template<typename DatabaseType>
void queryRecoveryRecords(const std::string& databasePath, int queryRecoveryRecords) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return;
    }
    
    auto records = db.getRecoveryRecords(queryRecoveryRecords);
    if (records.empty()) {
        if (queryRecoveryRecords >= 0) {
            std::println(std::cout, "No recovery records within the last {} days.", queryRecoveryRecords);
        } else {
            std::println(std::cout, "No recovery records.");
        }
    } else {
        if (queryRecoveryRecords >= 0) {
            std::println(std::cout, "Recovery records within the last {} days:", queryRecoveryRecords);
        } else {
            std::println(std::cout, "Recovery records:");
        }
        std::println(std::cout, "ID\tIP Address\tHostname\tAlert Time\t\tRecovery Time");
        std::println(std::cout, "------------------------------------------------------------------------------------------------");
        for (const auto& [id, ip, hostname, alertTime, recoveryTime] : records) {
            std::println(std::cout, "{}\t{}\t\t{}\t\t{}\t{}", id, ip, hostname, alertTime, recoveryTime);
        }
    }
}

// 模板函数：获取所有主机
template<typename DatabaseType>
std::map<std::string, std::string> getAllHosts(const std::string& databasePath) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return {};
    }
    return db.getAllHosts();
}

// 模板函数：插入ping结果
template<typename DatabaseType>
bool insertPingResults(const std::string& databasePath, 
                      const std::vector<std::tuple<std::string, std::string, bool, short, std::string>>& allResults) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return false;
    }
    
    // 将结果转换为数据库所需的格式并批量插入
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> dbResults;
    dbResults.reserve(allResults.size());
    
    for (const auto& [ip, hostname, result, delay, timestamp] : allResults) {
        dbResults.emplace_back(ip, hostname, delay, result, timestamp);
    }
    
    return db.insertPingResults(dbResults);
}

// 模板函数：处理告警逻辑
template<typename DatabaseType>
bool processAlerts(const std::string& databasePath,
                  const std::vector<std::tuple<std::string, std::string, bool, short, std::string>>& allResults) {
    DatabaseType db(databasePath);
    if (!initializeDatabase(databasePath, db)) {
        return false;
    }
    
    // 处理告警：主机状态不通时记录到告警表，主机状态正常时从告警表移除
    bool success = true;
    for (const auto& [ip, hostname, successFlag, delay, timestamp] : allResults) {
        if (!successFlag) {
            // 主机状态不通，添加到告警表
            if (!db.addAlert(ip, hostname)) {
                std::println(std::cerr, "Failed to add alert for IP: {}", ip);
                success = false;
            }
        } else {
            // 主机状态正常，从告警表移除
            db.removeAlert(ip);
        }
    }
    
    return success;
}

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
                std::println(std::cerr, "Database must be enabled to query statistics. Use -d option to specify database path.");
                return 1;
            }
            
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                queryIPStatistics<DatabaseManagerPG>(config.databasePath, config.queryIP);
            } else {
#endif
                queryIPStatistics<DatabaseManager>(config.databasePath, config.queryIP);
#ifdef USE_POSTGRESQL
            }
#endif
            return 0;
        }
        
        // 如果请求执行清理操作
        if (config.cleanupDays >= 0) {
            if (!config.enableDatabase) {
                std::println(std::cerr, "Database must be enabled to cleanup data. Use -d option to specify database path.");
                return 1;
            }
            
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                cleanupOldData<DatabaseManagerPG>(config.databasePath, config.cleanupDays);
            } else {
#endif
                cleanupOldData<DatabaseManager>(config.databasePath, config.cleanupDays);
#ifdef USE_POSTGRESQL
            }
#endif
            return 0;
        }
        
        // 如果请求查询告警，则只显示告警信息，不执行ping操作
        if (config.queryAlerts >= 0 || config.queryAlerts == -2) {  // -2表示启用告警查询（未指定天数），>=0表示查询指定天数内的告警
            if (!config.enableDatabase) {
                std::cerr << "Database must be enabled to query alerts. Use -d option to specify database path.\n";
                return 1;
            }
            
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                queryActiveAlerts<DatabaseManagerPG>(config.databasePath, config.queryAlerts);
            } else {
#endif
                queryActiveAlerts<DatabaseManager>(config.databasePath, config.queryAlerts);
#ifdef USE_POSTGRESQL
            }
#endif
            return 0;
        }
        
        // 如果请求查询恢复记录，则只显示恢复记录信息，不执行ping操作
        if (config.queryRecoveryRecords >= 0 || config.queryRecoveryRecords == -2) {  // -2表示启用恢复记录查询（未指定天数），>=0表示查询指定天数内的恢复记录
            if (!config.enableDatabase) {
                std::cerr << "Database must be enabled to query recovery records. Use -d option to specify database path.\n";
                return 1;
            }
            
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                queryRecoveryRecords<DatabaseManagerPG>(config.databasePath, config.queryRecoveryRecords);
            } else {
#endif
                queryRecoveryRecords<DatabaseManager>(config.databasePath, config.queryRecoveryRecords);
#ifdef USE_POSTGRESQL
            }
#endif
            return 0;
        }
        
        // 读取主机列表
        std::map<std::string, std::string> hosts;
        
        // 如果指定了文件名（通过-f参数或命令行参数），则从文件读取主机列表
        // 否则如果启用了数据库，则从数据库的hosts表读取主机列表
        // 如果两者都没有指定，则默认从ip.txt文件读取
        if (!config.filename.empty()) {
            hosts = readHostsFromFile(config.filename);
        } else if (config.enableDatabase) {
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                hosts = getAllHosts<DatabaseManagerPG>(config.databasePath);
            } else {
#endif
                hosts = getAllHosts<DatabaseManager>(config.databasePath);
#ifdef USE_POSTGRESQL
            }
#endif
        } else {
            // 默认从ip.txt文件读取
            hosts = readHostsFromFile("ip.txt");
        }
        
        if (hosts.empty()) {
            std::println(std::cerr, "No hosts to ping. Please check the input file or database.");
            return 1;
        }
        
        // 创建ping管理器并执行ping操作
        PingManager pingManager;
        // 使用默认最大并发数执行ping操作
        std::vector<std::tuple<std::string, std::string, bool, short, std::string>> allResults = 
            pingManager.performPing(hosts, config.pingCount, config.timeoutSeconds);
        
        // 如果启用了数据库，则初始化数据库管理器并存储结果
        if (config.enableDatabase) {
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                if (!insertPingResults<DatabaseManagerPG>(config.databasePath, allResults)) {
                    std::println(std::cerr, "Failed to insert ping results into PostgreSQL database");
                    return 1;
                }
            } else {
#endif
                if (!insertPingResults<DatabaseManager>(config.databasePath, allResults)) {
                    std::println(std::cerr, "Failed to insert ping results into database");
                    return 1;
                }
#ifdef USE_POSTGRESQL
            }
#endif
        }
        
        // 处理告警逻辑（仅在启用数据库时）
        if (config.enableDatabase) {
#ifdef USE_POSTGRESQL
            if (config.usePostgreSQL) {
                if (!processAlerts<DatabaseManagerPG>(config.databasePath, allResults)) {
                    return 1;
                }
            } else {
#endif
                if (!processAlerts<DatabaseManager>(config.databasePath, allResults)) {
                    return 1;
                }
#ifdef USE_POSTGRESQL
            }
#endif
        }
        
        // 打印所有IP地址和结果（除非启用静默模式）
        if (!config.silentMode) {
            for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
                std::println(std::cout, "{}\t{}\t{}\t{}ms", ip, hostname, (success ? "success" : "failed"), delay);
            }
        }
        
        return 0;
    } catch (const std::exception& e) {
        std::println(std::cerr, "Exception occurred: {}", e.what());
        return 1;
    } catch (...) {
        std::println(std::cerr, "Unknown exception occurred");
        return 1;
    }
}