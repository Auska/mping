#include "commands.h"

#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <unordered_set>
#include <vector>

#include "config_manager.h"
#include "constants.h"
#include "database_factory.h"
#include "database_interface.h"
#include "ping_manager.h"
#include "utils.h"

// ══════════════════════════════════════════════════════════════════════════
//  Command 基类
// ══════════════════════════════════════════════════════════════════════════

Command::Command(const ConfigManager::Config& cfg) : config(cfg) {
}

std::unique_ptr<DatabaseInterface> Command::createDatabase() {
    DatabaseType dbType = DatabaseType::SQLITE;

#ifdef USE_POSTGRESQL
    if (config.usePostgreSQL
        || DatabaseFactory::detectDatabaseType(config.databasePath) == DatabaseType::POSTGRESQL) {
        dbType = DatabaseType::POSTGRESQL;
    }
#else
    // POSTGRESQL 未启用时 detectDatabaseType 已返回 SQLITE
    dbType = DatabaseFactory::detectDatabaseType(config.databasePath);
#endif

    return DatabaseFactory::createDatabase(dbType, config.databasePath);
}

bool Command::initializeDatabase(DatabaseInterface& db) {
    if (!db.initialize()) {
        std::println(std::cerr, "Failed to initialize database");
        return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryIPCommand
// ══════════════════════════════════════════════════════════════════════════

int QueryIPCommand::execute() {
    auto db = createDatabase();
    if (!initializeDatabase(*db)) {
        return 1;
    }
    db->queryIPStatistics(config.queryIP);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
//  CleanupCommand
// ══════════════════════════════════════════════════════════════════════════

int CleanupCommand::execute() {
    auto db = createDatabase();
    if (!initializeDatabase(*db)) {
        return 1;
    }
    db->cleanupOldData(config.cleanupDays);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryAlertsCommand
// ══════════════════════════════════════════════════════════════════════════

int QueryAlertsCommand::execute() {
    auto db = createDatabase();
    if (!initializeDatabase(*db)) {
        return 1;
    }

    auto alerts = db->getActiveAlerts(config.queryAlerts);
    if (alerts.empty()) {
        if (config.queryAlerts >= 0) {
            std::println(std::cout, "No active alerts within the last {} days.",
                         config.queryAlerts);
        } else {
            std::println(std::cout, "No active alerts.");
        }
    } else {
        if (config.queryAlerts >= 0) {
            std::println(std::cout, "Active alerts within the last {} days:", config.queryAlerts);
        } else {
            std::println(std::cout, "Active alerts:");
        }
        std::println(std::cout, "IP Address\tHostname\tCreated Time");
        std::println(std::cout, "------------------------------------------------");
        for (const auto& [ip, hostname, createdTime] : alerts) {
            std::println(std::cout, "{}\t{}\t{}", ip, hostname, createdTime);
        }
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryRecoveryCommand
// ══════════════════════════════════════════════════════════════════════════

int QueryRecoveryCommand::execute() {
    auto db = createDatabase();
    if (!initializeDatabase(*db)) {
        return 1;
    }

    auto records = db->getRecoveryRecords(config.queryRecoveryRecords);
    if (records.empty()) {
        if (config.queryRecoveryRecords >= 0) {
            std::println(std::cout, "No recovery records within the last {} days.",
                         config.queryRecoveryRecords);
        } else {
            std::println(std::cout, "No recovery records.");
        }
    } else {
        if (config.queryRecoveryRecords >= 0) {
            std::println(std::cout,
                         "Recovery records within the last {} days:", config.queryRecoveryRecords);
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
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
//  PingCommand
// ══════════════════════════════════════════════════════════════════════════

bool PingCommand::insertPingResults(DatabaseInterface* db,
                                    const std::vector<PingResult>& allResults) {
    if (!db) {
        return false;
    }

    // 将结果转换为数据库所需的格式并批量插入
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> dbResults;
    dbResults.reserve(allResults.size());

    for (const auto& r : allResults) {
        dbResults.emplace_back(r.ip, r.hostname, r.delayMs, r.success, r.timestamp);
    }

    return db->insertPingResults(dbResults);
}

bool PingCommand::processAlerts(DatabaseInterface* db, const std::vector<PingResult>& allResults) {
    if (!db) {
        return false;
    }

    // 预查当前在告警表中的 IP 列表
    auto activeAlertTuples = db->getActiveAlerts();
    std::unordered_set<std::string> alertIPs;
    for (const auto& alert : activeAlertTuples) {
        alertIPs.insert(std::get<0>(alert));
    }

    // 只在实际状态变化时写入
    bool success = true;
    for (const auto& r : allResults) {
        bool isCurrentlyAlerted = alertIPs.count(r.ip) > 0;

        if (!r.success && !isCurrentlyAlerted) {
            // 刚刚不通，且之前没有告警 → 新增告警
            if (!db->addAlert(r.ip, r.hostname)) {
                std::println(std::cerr, "Failed to add alert for IP: {}", r.ip);
                success = false;
            }
        } else if (r.success && isCurrentlyAlerted) {
            // 刚刚恢复正常，且之前有告警 → 移除告警并记录恢复
            if (!db->removeAlert(r.ip)) {
                std::println(std::cerr, "Failed to remove alert for IP: {}", r.ip);
                success = false;
            }
        }
        // 状态无变化 → 跳过写操作
    }

    return success;
}

int PingCommand::execute() {
    // 读取主机列表
    std::map<std::string, std::string> hosts;

    // 如果指定了文件名（通过 -f 参数或命令行参数），则从文件读取主机列表
    // 否则如果启用了数据库，则从数据库的 hosts 表读取主机列表
    // 如果两者都没有指定，则默认从 ip.txt 文件读取
    if (!config.filename.empty()) {
        hosts = readHostsFromFile(config.filename);
    } else if (config.enableDatabase) {
        auto db = createDatabase();
        if (!initializeDatabase(*db)) {
            return 1;
        }
        hosts = db->getAllHosts();
    } else {
        hosts = readHostsFromFile(ConfigDefaults::DEFAULT_FILENAME);
    }

    if (hosts.empty()) {
        std::println(std::cerr, "No hosts to ping. Please check the input file or database.");
        return 1;
    }

    // 创建 Ping 管理器并执行 Ping 操作
    PingManager pingManager;
    auto allResults = pingManager.performPing(hosts);

    // 如果启用了数据库，则初始化数据库管理器并存储结果
    if (config.enableDatabase) {
        auto db = createDatabase();
        if (!initializeDatabase(*db)) {
            return 1;
        }

        if (!insertPingResults(db.get(), allResults)) {
            std::println(std::cerr, "Failed to insert ping results into database");
            return 1;
        }

        // 处理告警逻辑
        if (!processAlerts(db.get(), allResults)) {
            return 1;
        }

        // 每次检查后自动清理 ping_results 表中超过指定天数的旧记录
        db->cleanupOldPingResults(ConfigDefaults::DEFAULT_PING_RESULTS_CLEANUP_DAYS);
    }

    // 打印所有 IP 地址和结果（除非启用静默模式）
    if (!config.silentMode) {
        for (const auto& r : allResults) {
            std::println(std::cout, "{}\t{}\t{}\t{}ms", r.ip, r.hostname,
                         (r.success ? "success" : "failed"), r.delayMs);
        }
    }

    return 0;
}
