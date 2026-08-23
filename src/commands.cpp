#include "commands.h"

#include <chrono>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <print>
#include <thread>
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

bool Command::initializeDatabase(DatabaseManagerPG& db, bool precreatePartitions) {
    const bool ok = precreatePartitions ? db.initialize() : db.initializeForQuery();
    if (!ok) {
        std::println(std::cerr, "Failed to initialize database");
    }
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryIPCommand
// ══════════════════════════════════════════════════════════════════════════

int QueryIPCommand::execute() {
    auto db = createDatabase();
    if (!initializeDatabase(*db, /*precreatePartitions=*/false)) {
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
    if (!initializeDatabase(*db, /*precreatePartitions=*/false)) {
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
    if (!initializeDatabase(*db, /*precreatePartitions=*/false)) {
        return 1;
    }

    auto alerts = db->getActiveAlerts(config.queryAlerts);
    const std::string daysSuffix =
        config.queryAlerts >= 0 ? " within the last " + std::to_string(config.queryAlerts) + " days"
                                : "";
    if (alerts.empty()) {
        std::println(std::cout, "No active alerts{}.", daysSuffix);
    } else {
        std::println(std::cout, "Active alerts{}:", daysSuffix);
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
    if (!initializeDatabase(*db, /*precreatePartitions=*/false)) {
        return 1;
    }

    auto records = db->getRecoveryRecords(config.queryRecoveryRecords);
    const std::string daysSuffix =
        config.queryRecoveryRecords >= 0
            ? " within the last " + std::to_string(config.queryRecoveryRecords) + " days"
            : "";
    if (records.empty()) {
        std::println(std::cout, "No recovery records{}.", daysSuffix);
    } else {
        std::println(std::cout, "Recovery records{}:", daysSuffix);
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

bool PingCommand::processAlerts(DatabaseManagerPG* db, const std::vector<PingResult>& allResults,
                                const std::unordered_set<std::string>& alertIPs) {
    if (!db) {
        return false;
    }

    // 只在实际状态变化时写入；新增告警聚合为单次批量 UPSERT（单条语句）
    bool success = true;
    std::vector<std::tuple<std::string, std::string>> newAlerts;
    for (const auto& r : allResults) {
        bool isCurrentlyAlerted = alertIPs.count(r.ip) > 0;

        if (!r.success && !isCurrentlyAlerted) {
            // 刚刚不通，且之前没有告警 → 待新增告警（批量写入）
            newAlerts.emplace_back(r.ip, r.hostname);
        } else if (r.success && isCurrentlyAlerted) {
            // 刚刚恢复正常，且之前有告警 → 移除告警并记录恢复
            if (!db->removeAlert(r.ip)) {
                std::println(std::cerr, "Failed to remove alert for IP: {}", r.ip);
                success = false;
            }
        }
        // 状态无变化 → 跳过写操作
    }
    if (!newAlerts.empty() && !db->addAlerts(newAlerts)) {
        std::println(std::cerr, "Failed to add alerts for {} IP(s)", newAlerts.size());
        success = false;
    }

    return success;
}

bool PingCommand::loadHosts(std::unique_ptr<DatabaseManagerPG>& db,
                            std::map<std::string, std::string>& hosts) {
    // 主机来源优先级：-f 文件 > 数据库 hosts 表 > 默认 ip.txt
    if (!config.filename.empty()) {
        hosts = readHostsFromFile(config.filename);
    } else if (useDatabase()) {
        db = createDatabase();
        if (!initializeDatabase(*db)) {
            return false;
        }
        hosts = db->getAllHosts();
    } else {
        hosts = readHostsFromFile(ConfigDefaults::DEFAULT_FILENAME);
    }

    if (hosts.empty()) {
        std::println(std::cerr, "No hosts to ping. Please check the input file or database.");
        return false;
    }
    return true;
}

// 落库、告警处理与自动清理；失败返回 false（调用方统一返回退出码 1）
bool PingCommand::persistResults(std::vector<PingResult>& allResults,
                                 std::unique_ptr<DatabaseManagerPG>& db,
                                 const std::unordered_set<std::string>& alertIPs) {
    if (!db) {
        db = createDatabase();
        if (!initializeDatabase(*db)) {
            return false;
        }
    }

    if (!insertPingResults(db.get(), allResults)) {
        std::println(std::cerr, "Failed to insert ping results into database");
        return false;
    }

    // 处理告警（只写状态变化；瞬时波动已由检查阶段的滑动窗口过滤）
    if (!processAlerts(db.get(), allResults, alertIPs)) {
        return false;
    }

    // 自动清理 ping_results 过期记录（24h 节流，见 cleanupOldPingResults）
    db->cleanupOldPingResults(ConfigDefaults::DEFAULT_PING_RESULTS_CLEANUP_DAYS);
    return true;
}

void PingCommand::printResults(const std::vector<PingResult>& allResults) {
    for (const auto& r : allResults) {
        if (r.success) {
            std::println(std::cout, "{}\t{}\tsuccess\t{}ms", r.ip, r.hostname, r.delayMs);
        } else {
            std::println(std::cout, "{}\t{}\tfailed\t-", r.ip, r.hostname);
        }
    }
}

int PingCommand::execute() {
    // 持续检查模式：checkIntervalSeconds > 0 时按固定间隔循环执行（0 = 单次运行，向后兼容）。
    // 数据库连接与 PingManager 线程池跨轮复用；主机清单与告警状态每轮重新加载，变更即时生效。
    // ponytail: 任何一轮失败即中止（返回 1），由外部 supervisor 重启；如需自愈可改为记录错误后继续。
    std::unique_ptr<DatabaseManagerPG> db;
    std::unordered_set<std::string> alertIPs;
    PingManager pingManager;

    for (;;) {
        // 1. 加载主机列表（文件 / 数据库 hosts 表 / 默认文件）
        std::map<std::string, std::string> hosts;
        if (!loadHosts(db, hosts)) {
            return 1;
        }

        // 2. 数据库模式：查询当前告警状态（每轮刷新，告警新增/恢复判定依赖最新状态）
        if (useDatabase()) {
            if (!db) {
                db = createDatabase();
                if (!initializeDatabase(*db)) {
                    return 1;
                }
            }
            alertIPs.clear();
            for (const auto& alert : db->getActiveAlerts()) {
                alertIPs.insert(std::get<0>(alert));
            }
        }

        // 3. 滑动窗口并发检查：所有主机统一连续 DOWN_CONFIRM_WINDOW 轮失败才判定离线，对抗瞬时波动
        std::vector<PingResult> allResults =
            pingManager.checkHosts(hosts, ConfigDefaults::DOWN_CONFIRM_WINDOW);

        // 4. 落库、告警处理与自动清理（仅数据库模式）
        if (useDatabase()) {
            if (!persistResults(allResults, db, alertIPs)) {
                return 1;
            }
            // 配置了 cleanup_days 时，持续模式每轮执行全量清理（alerts/recovery/ping_results 同 -C）
            if (config.cleanupDays >= 0) {
                db->cleanupOldData(config.cleanupDays);
            }
        }

        // 5. 输出结果（静默模式跳过）
        if (!config.silentMode) {
            printResults(allResults);
        }

        // 持续检查模式：轮间等待后进入下一轮；Ctrl+C 直接终止进程（结果已逐轮落盘，无丢失）
        if (config.checkIntervalSeconds <= 0) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(config.checkIntervalSeconds));
    }
}
