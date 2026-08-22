#include "commands.h"

#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config_manager.h"
#include "constants.h"
#include "database_manager_pg.h"
#include "ping_manager.h"
#include "utils.h"

// ══════════════════════════════════════════════════════════════════════════
//  Command 基类
// ══════════════════════════════════════════════════════════════════════════

Command::Command(const ConfigManager::Config& cfg) : config(cfg) {
}

std::unique_ptr<DatabaseManagerPG> Command::createDatabase() {
    return std::make_unique<DatabaseManagerPG>(config.databasePath);
}

bool Command::initializeDatabase(DatabaseManagerPG& db) {
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

bool PingCommand::insertPingResults(DatabaseManagerPG* db,
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

bool PingCommand::confirmFailuresWithRetry(PingManager& pingManager,
                                           std::vector<PingResult>& allResults,
                                           DatabaseManagerPG* db,
                                           const std::unordered_set<std::string>& alertIPs) {
    if (!db) {
        return false;
    }

    // 收集首次检查不通且未告警的主机（已告警主机处于持续故障，无需重复确认）
    std::map<std::string, std::string> pendingHosts;
    for (const auto& r : allResults) {
        if (!r.success && alertIPs.count(r.ip) == 0) {
            pendingHosts[r.ip] = r.hostname;
        }
    }

    if (pendingHosts.empty()) {
        return true;
    }

    if (!config.silentMode) {
        std::println(std::cout,
                     "Retrying {} unreachable host(s) {} time(s) to confirm before alerting...",
                     pendingHosts.size(), ConfigDefaults::ALERT_CONFIRM_RETRY_COUNT);
    }

    auto retryResults =
        pingManager.retryHosts(pendingHosts, ConfigDefaults::ALERT_CONFIRM_RETRY_COUNT);

    // 用重试确认的结果更新输出：仅当重试成功时覆盖为成功状态
    std::unordered_map<std::string, PingResult> retryByIp;
    retryByIp.reserve(retryResults.size());
    for (const auto& rr : retryResults) {
        if (rr.success) {
            retryByIp[rr.ip] = rr;
        }
    }
    for (auto& r : allResults) {
        auto it = retryByIp.find(r.ip);
        if (it != retryByIp.end()) {
            r = it->second;
        }
    }

    return true;
}

bool PingCommand::processAlerts(DatabaseManagerPG* db, const std::vector<PingResult>& allResults,
                                const std::unordered_set<std::string>& alertIPs) {
    if (!db) {
        return false;
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
    // 如果指定了文件名（通过 -f 参数或命令行参数），则从文件读取主机列表
    // 否则如果启用了数据库，则从数据库的 hosts 表读取主机列表
    // 如果两者都没有指定，则默认从 ip.txt 文件读取
    // db 在启用数据库时创建一次，供取主机与存结果复用
    std::unique_ptr<DatabaseManagerPG> db;
    std::map<std::string, std::string> hosts;

    if (!config.filename.empty()) {
        hosts = readHostsFromFile(config.filename);
    } else if (config.enableDatabase) {
        db = createDatabase();
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

    // 如果启用了数据库，则存储结果并处理告警
    if (config.enableDatabase) {
        if (!db) {
            db = createDatabase();
            if (!initializeDatabase(*db)) {
                return 1;
            }
        }

        // 预查当前在告警表中的 IP（一次查询，确认重试与告警处理共用）
        std::unordered_set<std::string> alertIPs;
        for (const auto& alert : db->getActiveAlerts()) {
            alertIPs.insert(std::get<0>(alert));
        }

        // 首次不通且未告警的主机，先重试确认再决定是否告警（避免误报）
        if (!confirmFailuresWithRetry(pingManager, allResults, db.get(), alertIPs)) {
            return 1;
        }

        if (!insertPingResults(db.get(), allResults)) {
            std::println(std::cerr, "Failed to insert ping results into database");
            return 1;
        }

        // 处理告警逻辑
        if (!processAlerts(db.get(), allResults, alertIPs)) {
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
