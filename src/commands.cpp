#include "commands.h"

#include <iostream>
#include <iterator>
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

bool PingCommand::loadHosts(std::unique_ptr<DatabaseManagerPG>& db,
                            std::map<std::string, std::string>& hosts) {
    // 主机来源优先级：-f 文件 > 数据库 hosts 表 > 默认 ip.txt
    if (!config.filename.empty()) {
        hosts = readHostsFromFile(config.filename);
    } else if (config.enableDatabase) {
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

    // 每次检查后自动清理 ping_results 表中超过指定天数的旧记录
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
    // 1. 加载主机列表（文件 / 数据库 hosts 表 / 默认文件）
    std::unique_ptr<DatabaseManagerPG> db;
    std::map<std::string, std::string> hosts;
    if (!loadHosts(db, hosts)) {
        return 1;
    }

    // 2. 数据库模式：先查询当前告警状态（滑动窗口分级与告警处理共用一次查询）
    std::unordered_set<std::string> alertIPs;
    if (config.enableDatabase) {
        if (!db) {
            db = createDatabase();
            if (!initializeDatabase(*db)) {
                return 1;
            }
        }
        for (const auto& alert : db->getActiveAlerts()) {
            alertIPs.insert(std::get<0>(alert));
        }
    }

    // 3. 滑动窗口并发检查（取代原先"快速探测 + 失败重试 + 告警确认重试"的重叠流程）：
    //    - 未告警主机：连续 DOWN_CONFIRM_WINDOW 轮失败才判定离线，对抗瞬时波动，避免误报告警
    //    - 已告警主机（持续故障）：单轮快检，只记录当前状态
    std::map<std::string, std::string> alertedHosts;
    std::map<std::string, std::string> pendingHosts;
    for (const auto& [ip, hostname] : hosts) {
        if (alertIPs.count(ip)) {
            alertedHosts[ip] = hostname;
        } else {
            pendingHosts[ip] = hostname;
        }
    }

    PingManager pingManager;
    std::vector<PingResult> allResults =
        pingManager.checkHosts(pendingHosts, ConfigDefaults::DOWN_CONFIRM_WINDOW);
    auto alertedResults = pingManager.checkHosts(alertedHosts, 1);
    allResults.insert(allResults.end(), std::make_move_iterator(alertedResults.begin()),
                      std::make_move_iterator(alertedResults.end()));

    // 4. 落库、告警处理与自动清理（仅数据库模式）
    if (config.enableDatabase) {
        if (!persistResults(allResults, db, alertIPs)) {
            return 1;
        }
    }

    // 5. 输出结果（静默模式跳过）
    if (!config.silentMode) {
        printResults(allResults);
    }

    return 0;
}
