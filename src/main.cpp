#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <string>
#include <vector>

#include "config_manager.h"
#include "database_factory.h"
#include "database_interface.h"
#include "ping_manager.h"
#include "utils.h"

// 函数：初始化数据库
bool initializeDatabase(DatabaseInterface* db) {
    if (!db || !db->initialize()) {
        std::println(std::cerr, "Failed to initialize database");
        return false;
    }
    return true;
}

// 函数：查询IP统计信息
void queryIPStatistics(DatabaseInterface* db, const std::string& queryIP) {
    if (!db) {
        return;
    }
    db->queryIPStatistics(queryIP);
}

// 函数：清理旧数据
void cleanupOldData(DatabaseInterface* db, int cleanupDays) {
    if (!db) {
        return;
    }
    db->cleanupOldData(cleanupDays);
}

// 函数：查询活动告警
void queryActiveAlerts(DatabaseInterface* db, int queryAlerts) {
    if (!db) {
        return;
    }

    auto alerts = db->getActiveAlerts(queryAlerts);
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

// 函数：查询恢复记录
void queryRecoveryRecords(DatabaseInterface* db, int queryRecoveryRecords) {
    if (!db) {
        return;
    }

    auto records = db->getRecoveryRecords(queryRecoveryRecords);
    if (records.empty()) {
        if (queryRecoveryRecords >= 0) {
            std::println(std::cout, "No recovery records within the last {} days.",
                         queryRecoveryRecords);
        } else {
            std::println(std::cout, "No recovery records.");
        }
    } else {
        if (queryRecoveryRecords >= 0) {
            std::println(std::cout,
                         "Recovery records within the last {} days:", queryRecoveryRecords);
        } else {
            std::println(std::cout, "Recovery records:");
        }
        std::println(std::cout, "IP Address\tHostname\tAlert Time\t\tRecovery Time");
        std::println(std::cout,
                     "-----------------------------------------------------------------------------"
                     "-----------");
        for (const auto& [id, ip, hostname, alertTime, recoveryTime] : records) {
            std::println(std::cout, "{}\t\t{}\t\t{}\t{}", ip, hostname, alertTime, recoveryTime);
        }
    }
}

// 函数：获取所有主机
std::map<std::string, std::string> getAllHosts(DatabaseInterface* db) {
    if (!db) {
        return {};
    }
    return db->getAllHosts();
}

// 函数：插入ping结果
bool insertPingResults(
    DatabaseInterface* db,
    const std::vector<std::tuple<std::string, std::string, bool, short, std::string>>& allResults) {
    if (!db) {
        return false;
    }

    // 将结果转换为数据库所需的格式并批量插入
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> dbResults;
    dbResults.reserve(allResults.size());

    for (const auto& [ip, hostname, result, delay, timestamp] : allResults) {
        dbResults.emplace_back(ip, hostname, delay, result, timestamp);
    }

    return db->insertPingResults(dbResults);
}

// 函数：处理告警逻辑
bool processAlerts(
    DatabaseInterface* db,
    const std::vector<std::tuple<std::string, std::string, bool, short, std::string>>& allResults) {
    if (!db) {
        return false;
    }

    // 处理告警：主机状态不通时记录到告警表，主机状态正常时从告警表移除
    bool success = true;
    for (const auto& [ip, hostname, successFlag, delay, timestamp] : allResults) {
        if (!successFlag) {
            // 主机状态不通，添加到告警表
            if (!db->addAlert(ip, hostname)) {
                std::println(std::cerr, "Failed to add alert for IP: {}", ip);
                success = false;
            }
        } else {
            // 主机状态正常，从告警表移除
            db->removeAlert(ip);
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

        // 确定数据库类型
        DatabaseType dbType = DatabaseType::SQLITE;  // 默认使用SQLite
#ifdef USE_POSTGRESQL
        if (config.usePostgreSQL
            || DatabaseFactory::detectDatabaseType(config.databasePath)
                   == DatabaseType::POSTGRESQL) {
            dbType = DatabaseType::POSTGRESQL;
        }
#else
        // 如果没有启用PostgreSQL支持，仅根据连接字符串检测
        if (DatabaseFactory::detectDatabaseType(config.databasePath) == DatabaseType::POSTGRESQL) {
            dbType = DatabaseType::POSTGRESQL;
        }
#endif

        // 创建数据库实例（延迟创建，仅在需要时）
        std::unique_ptr<DatabaseInterface> db;

        // 如果提供了查询IP，则只显示查询结果，不执行ping操作
        if (!config.queryIP.empty()) {
            if (!config.enableDatabase) {
                std::println(std::cerr,
                             "Database must be enabled to query statistics. Use -d option to "
                             "specify database path.");
                return 1;
            }

            db = DatabaseFactory::createDatabase(dbType, config.databasePath);
            if (!initializeDatabase(db.get())) {
                return 1;
            }
            queryIPStatistics(db.get(), config.queryIP);
            return 0;
        }

        // 如果请求执行清理操作
        if (config.cleanupDays >= 0) {
            if (!config.enableDatabase) {
                std::println(std::cerr,
                             "Database must be enabled to cleanup data. Use -d option to specify "
                             "database path.");
                return 1;
            }

            db = DatabaseFactory::createDatabase(dbType, config.databasePath);
            if (!initializeDatabase(db.get())) {
                return 1;
            }
            cleanupOldData(db.get(), config.cleanupDays);
            return 0;
        }

        // 如果请求查询告警，则只显示告警信息，不执行ping操作
        if (config.queryAlerts >= 0
            || config.queryAlerts
                   == -2) {  // -2表示启用告警查询（未指定天数），>=0表示查询指定天数内的告警
            if (!config.enableDatabase) {
                std::println(std::cerr,
                             "Database must be enabled to query alerts. Use -d option to specify "
                             "database path.");
                return 1;
            }

            db = DatabaseFactory::createDatabase(dbType, config.databasePath);
            if (!initializeDatabase(db.get())) {
                return 1;
            }
            queryActiveAlerts(db.get(), config.queryAlerts);
            return 0;
        }

        // 如果请求查询恢复记录，则只显示恢复记录信息，不执行ping操作
        if (config.queryRecoveryRecords >= 0
            || config.queryRecoveryRecords
                   == -2) {  // -2表示启用恢复记录查询（未指定天数），>=0表示查询指定天数内的恢复记录
            if (!config.enableDatabase) {
                std::println(std::cerr,
                             "Database must be enabled to query recovery records. Use -d option to "
                             "specify database path.");
                return 1;
            }

            db = DatabaseFactory::createDatabase(dbType, config.databasePath);
            if (!initializeDatabase(db.get())) {
                return 1;
            }
            queryRecoveryRecords(db.get(), config.queryRecoveryRecords);
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
            db = DatabaseFactory::createDatabase(dbType, config.databasePath);
            if (!initializeDatabase(db.get())) {
                return 1;
            }
            hosts = getAllHosts(db.get());
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
            // 如果数据库实例尚未创建，则创建
            if (!db) {
                db = DatabaseFactory::createDatabase(dbType, config.databasePath);
                if (!initializeDatabase(db.get())) {
                    return 1;
                }
            }

            if (!insertPingResults(db.get(), allResults)) {
                if (dbType == DatabaseType::POSTGRESQL) {
                    std::println(std::cerr,
                                 "Failed to insert ping results into PostgreSQL database");
                } else {
                    std::println(std::cerr, "Failed to insert ping results into database");
                }
                return 1;
            }

            // 处理告警逻辑
            if (!processAlerts(db.get(), allResults)) {
                return 1;
            }
        }

        // 打印所有IP地址和结果（除非启用静默模式）
        if (!config.silentMode) {
            for (const auto& [ip, hostname, success, delay, timestamp] : allResults) {
                std::println(std::cout, "{}\t{}\t{}\t{}ms", ip, hostname,
                             (success ? "success" : "failed"), delay);
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
