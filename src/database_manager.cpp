#include "database_manager.h"

#include <sqlite3.h>

#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <print>
#include <stdexcept>
#include <vector>

#include "statistics_printer.h"

DatabaseManager::DatabaseManager(const std::string& path) : dbPath(path), db(nullptr) {
    if (path.empty()) {
        throw std::invalid_argument("Database path cannot be empty");
    }
}

bool DatabaseManager::execSQL(const char* sql, const std::string& context) {
    char* errMsg = nullptr;
    int rc       = sqlite3_exec(db.get(), sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::string errorMsg = "SQL error " + context + ": ";
        if (errMsg) {
            errorMsg += errMsg;
            sqlite3_free(errMsg);
        } else {
            errorMsg += "Unknown error";
        }
        std::println(std::cerr, "{}", errorMsg);
        return false;
    }
    return true;
}

bool DatabaseManager::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* rawDb = nullptr;
    int rc         = sqlite3_open(dbPath.c_str(), &rawDb);
    if (rc) {
        std::string errorMsg = "Can't open database: ";
        if (rawDb) {
            errorMsg += sqlite3_errmsg(rawDb);
            sqlite3_close(rawDb);
        } else {
            errorMsg += "Unknown error";
        }
        std::println(std::cerr, "{}", errorMsg);
        return false;
    }

    if (!rawDb) {
        std::println(std::cerr, "Database handle is null after opening");
        return false;
    }

    // 将原始指针转移给智能指针管理
    db.reset(rawDb);

    // 并发写入时等待锁而不是立即失败（默认 busy timeout 为 0）
    sqlite3_busy_timeout(db.get(), 1000);

    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TEXT DEFAULT CURRENT_TIMESTAMP,
            last_seen TEXT,
            last_status BOOLEAN,
            last_delay INTEGER
        );
    )";

    if (!execSQL(createHostsTableSQL, "creating hosts table")) return false;

    // 创建ping_results表，用于存储所有ping结果
    const char* createPingResultsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS ping_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ip TEXT NOT NULL,
            hostname TEXT,
            delay INTEGER,
            success BOOLEAN,
            timestamp TEXT NOT NULL
        );
    )";

    if (!execSQL(createPingResultsTableSQL, "creating ping_results table")) return false;

    // 为ping_results表的ip和timestamp列创建索引以提高查询性能
    const char* createPingResultsIndexSQL = R"(
        CREATE INDEX IF NOT EXISTS idx_ping_results_ip ON ping_results (ip);
        CREATE INDEX IF NOT EXISTS idx_ping_results_timestamp ON ping_results (timestamp);
    )";

    if (!execSQL(createPingResultsIndexSQL, "creating indexes for ping_results table"))
        return false;

    // 创建alerts表，用于存储告警信息
    const char* createAlertsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS alerts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TEXT
        );
    )";

    if (!execSQL(createAlertsTableSQL, "creating alerts table")) return false;

    // 创建recovery_records表，用于存储已恢复主机的记录
    const char* createRecoveryTableSQL = R"(
        CREATE TABLE IF NOT EXISTS recovery_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ip TEXT,
            hostname TEXT,
            alert_time TEXT,
            recovery_time TEXT DEFAULT CURRENT_TIMESTAMP
        );
    )";

    return execSQL(createRecoveryTableSQL, "creating recovery_records table");
}

int DatabaseManager::execDelete(const char* table, int days) {
    std::string sql =
        std::format("DELETE FROM {} WHERE {} < datetime('now', '-' || ? || ' days');", table,
                    std::string(table) == "ping_results" ? "timestamp" : "created_time");

    // recovery_records uses recovery_time instead of created_time
    if (std::string(table) == "recovery_records") {
        sql = std::
            format("DELETE FROM {} WHERE recovery_time < datetime('now', '-' || ? || ' days');",
                   table);
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare delete statement for {}: {}", table,
                     sqlite3_errmsg(db.get()));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, days);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::println(std::cerr, "Failed to execute delete statement for {}: {}", table,
                     sqlite3_errmsg(db.get()));
        sqlite3_finalize(stmt);
        return -1;
    }

    int deleted = sqlite3_changes(db.get());
    sqlite3_finalize(stmt);
    return deleted;
}

bool DatabaseManager::insertPingResult(const std::string& ip, const std::string& hostname,
                                       short delay, bool success, const std::string& timestamp) {
    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 创建一个包含单个结果的向量并调用批量插入函数
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results;
    results.emplace_back(ip, hostname, delay, success, timestamp);
    return insertPingResults(results);
}

// 辅助函数：批量插入或更新主机信息
bool DatabaseManager::upsertHosts(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (results.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    // 使用批量插入优化性能
    const char* upsertHostSQL = R"(
        INSERT INTO hosts (ip, hostname, last_seen, last_status, last_delay)
        VALUES (?1, ?2, datetime('now', 'utc'), ?3, ?4)
        ON CONFLICT(ip) DO UPDATE SET
        hostname = excluded.hostname,
        last_seen = excluded.last_seen,
        last_status = excluded.last_status,
        last_delay = excluded.last_delay;
    )";

    sqlite3_stmt* hostStmt;
    int rc = sqlite3_prepare_v2(db.get(), upsertHostSQL, -1, &hostStmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare host statement: {}", sqlite3_errmsg(db.get()));
        return false;
    }

    bool success = true;
    // 开始事务以提高性能
    rc = sqlite3_exec(db.get(), "BEGIN;", 0, 0, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to begin transaction for hosts: {}",
                     sqlite3_errmsg(db.get()));
        sqlite3_finalize(hostStmt);
        return false;
    }

    // 为每个结果执行主机信息插入/更新
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        sqlite3_bind_text(hostStmt, 1, ip.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(hostStmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(hostStmt, 3, successFlag ? 1 : 0);
        sqlite3_bind_int(hostStmt, 4, delay);

        rc = sqlite3_step(hostStmt);
        if (rc != SQLITE_DONE) {
            std::println(std::cerr, "Failed to execute host statement: {}",
                         sqlite3_errmsg(db.get()));
            success = false;
            break;
        }

        // 重置语句以供下一次使用
        sqlite3_reset(hostStmt);
    }

    // 提交或回滚事务
    if (success) {
        rc = sqlite3_exec(db.get(), "COMMIT;", 0, 0, 0);
        if (rc != SQLITE_OK) {
            std::println(std::cerr, "Failed to commit transaction for hosts: {}",
                         sqlite3_errmsg(db.get()));
            // 事务未关闭，必须回滚避免残留（否则后续 BEGIN 全部失败）
            sqlite3_exec(db.get(), "ROLLBACK;", 0, 0, 0);
            success = false;
        }
    } else {
        sqlite3_exec(db.get(), "ROLLBACK;", 0, 0, 0);
    }

    sqlite3_finalize(hostStmt);
    return success;
}

// 辅助函数：批量插入ping结果
bool DatabaseManager::insertPingResultsBatch(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (results.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    // 准备插入语句到统一的ping_results表
    const char* insertSQL =
        "INSERT INTO ping_results (ip, hostname, delay, success, timestamp) VALUES (?1, ?2, ?3, "
        "?4, ?5);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db.get(), insertSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare ping results insert statement: {}",
                     sqlite3_errmsg(db.get()));
        return false;
    }

    bool success = true;

    // 开始事务以提高性能
    rc = sqlite3_exec(db.get(), "BEGIN;", 0, 0, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to begin transaction for ping results: {}",
                     sqlite3_errmsg(db.get()));
        sqlite3_finalize(stmt);
        return false;
    }

    // 为每个结果执行插入
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, delay);
        sqlite3_bind_int(stmt, 4, successFlag ? 1 : 0);
        sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::println(std::cerr, "Failed to execute ping results insert statement: {}",
                         sqlite3_errmsg(db.get()));
            success = false;
            break;
        }

        // 重置语句以供下一次使用
        sqlite3_reset(stmt);
    }

    // 提交或回滚事务
    if (success) {
        rc = sqlite3_exec(db.get(), "COMMIT;", 0, 0, 0);
        if (rc != SQLITE_OK) {
            std::println(std::cerr, "Failed to commit transaction for ping results: {}",
                         sqlite3_errmsg(db.get()));
            // 事务未关闭，必须回滚避免残留（否则后续 BEGIN 全部失败）
            sqlite3_exec(db.get(), "ROLLBACK;", 0, 0, 0);
            success = false;
        }
    } else {
        sqlite3_exec(db.get(), "ROLLBACK;", 0, 0, 0);
    }

    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::insertPingResults(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    if (results.empty()) {
        return true;  // 没有结果需要插入，视为成功
    }

    bool success = true;

    // 验证所有IP地址格式
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        if (!isValidIP(ip)) {
            std::println(std::cerr, "Invalid IP address format: {}", ip);
            return false;
        }
    }

    // 批量插入或更新主机信息
    if (success) {
        success = upsertHosts(results);
    }

    // 批量插入ping结果
    if (success) {
        success = insertPingResultsBatch(results);
    }

    return success;
}

std::string DatabaseManager::queryHostName(const std::string& ip) {
    const char* sql = "SELECT hostname FROM hosts WHERE ip = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt, 0) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    std::string hostname;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* text = (const char*)sqlite3_column_text(stmt, 0);
        if (text) hostname = text;
    }
    sqlite3_finalize(stmt);
    return hostname;
}

PingStatistics DatabaseManager::queryStatistics(const std::string& ip) {
    PingStatistics stats;
    const char* sql = R"(
        SELECT COUNT(*),
               SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END),
               AVG(CASE WHEN success = 1 THEN delay ELSE NULL END),
               MAX(CASE WHEN success = 1 THEN delay ELSE NULL END),
               MIN(CASE WHEN success = 1 THEN delay ELSE NULL END)
        FROM ping_results WHERE ip = ?;
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt, 0) != SQLITE_OK) {
        return stats;
    }
    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats.totalRecords = sqlite3_column_int(stmt, 0);
        stats.successCount = sqlite3_column_int(stmt, 1);
        stats.avgDelay =
            sqlite3_column_type(stmt, 2) != SQLITE_NULL ? sqlite3_column_double(stmt, 2) : 0;
        stats.maxDelay =
            sqlite3_column_type(stmt, 3) != SQLITE_NULL ? sqlite3_column_int(stmt, 3) : 0;
        stats.minDelay =
            sqlite3_column_type(stmt, 4) != SQLITE_NULL ? sqlite3_column_int(stmt, 4) : 0;
        stats.failureCount = stats.totalRecords - stats.successCount;
        if (stats.totalRecords > 0) {
            stats.successRate = (double)stats.successCount / stats.totalRecords * 100;
            stats.failureRate = (double)stats.failureCount / stats.totalRecords * 100;
        }
    }
    sqlite3_finalize(stmt);
    return stats;
}

void DatabaseManager::printRecentRecords(const std::string& ip) {
    const char* sql =
        "SELECT delay, success, timestamp FROM ping_results WHERE ip = ? "
        "ORDER BY timestamp DESC LIMIT 10;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt, 0) != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare recent records statement: {}",
                     sqlite3_errmsg(db.get()));
        return;
    }
    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    StatisticsPrinter::printRecentRecordsHeader();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* timestamp = (const char*)sqlite3_column_text(stmt, 2);
        StatisticsPrinter::printRecentRecordRow(timestamp ? timestamp : "N/A",
                                                sqlite3_column_int(stmt, 0),
                                                sqlite3_column_int(stmt, 1) != 0);
    }
    sqlite3_finalize(stmt);
}

void DatabaseManager::queryIPStatistics(const std::string& ip) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    auto hostname = queryHostName(ip);
    StatisticsPrinter::printHeader(ip, hostname);

    auto stats = queryStatistics(ip);
    StatisticsPrinter::printBody(stats);

    if (stats.totalRecords > 0) {
        printRecentRecords(ip);
    }
}

void DatabaseManager::cleanupOldData(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    std::println(std::cout, "Cleaning up data older than {} days...", days);

    int totalDeleted = execDelete("ping_results", days);
    if (totalDeleted < 0) return;
    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);

    int deletedAlerts = execDelete("alerts", days);
    if (deletedAlerts < 0) return;
    if (deletedAlerts > 0) {
        std::println(std::cout, "Deleted {} old alert records", deletedAlerts);
    }

    int deletedRecovery = execDelete("recovery_records", days);
    if (deletedRecovery < 0) return;
    if (deletedRecovery > 0) {
        std::println(std::cout, "Deleted {} old recovery records", deletedRecovery);
    }

    std::println(std::cout, "Cleanup completed.");
}

void DatabaseManager::cleanupOldPingResults(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    std::println(std::cout, "Cleaning up ping_results older than {} days...", days);

    const char* deleteSQL =
        "DELETE FROM ping_results WHERE timestamp < datetime('now', '-' || ? || ' days');";
    sqlite3_stmt* deleteStmt;
    int rc = sqlite3_prepare_v2(db.get(), deleteSQL, -1, &deleteStmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare delete statement: {}", sqlite3_errmsg(db.get()));
        return;
    }

    sqlite3_bind_int(deleteStmt, 1, days);
    rc = sqlite3_step(deleteStmt);
    if (rc != SQLITE_DONE) {
        std::println(std::cerr, "Failed to execute delete statement: {}", sqlite3_errmsg(db.get()));
        sqlite3_finalize(deleteStmt);
        return;
    }
    sqlite3_finalize(deleteStmt);

    int totalDeleted = sqlite3_changes(db.get());
    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);
}

std::map<std::string, std::string> DatabaseManager::getAllHosts() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::map<std::string, std::string> hosts;

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return hosts;
    }

    // 查询所有主机，按IP排序以提高查询效率
    const char* selectHostsSQL = "SELECT ip, hostname FROM hosts ORDER BY ip;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db.get(), selectHostsSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare hosts query statement: {}",
                     sqlite3_errmsg(db.get()));
        return hosts;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* ip       = (const char*)sqlite3_column_text(stmt, 0);
        const char* hostname = (const char*)sqlite3_column_text(stmt, 1);

        if (ip) {
            std::string ipStr       = ip;
            std::string hostnameStr = hostname ? hostname : "";
            hosts[ipStr]            = hostnameStr;
        }
    }

    sqlite3_finalize(stmt);
    return hosts;
}

bool DatabaseManager::addAlert(const std::string& ip, const std::string& hostname) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 插入或更新告警记录
    const char* insertAlertSQL = R"(
        INSERT INTO alerts (ip, hostname, created_time)
        VALUES (?, ?, datetime('now', 'utc'))
        ON CONFLICT(ip) DO NOTHING;
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db.get(), insertAlertSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare alert insert statement: {}",
                     sqlite3_errmsg(db.get()));
        return false;
    }

    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hostname.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::println(std::cerr, "Failed to execute alert insert statement: {}",
                     sqlite3_errmsg(db.get()));
        return false;
    }

    return true;
}

bool DatabaseManager::removeAlert(const std::string& ip) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 获取告警信息，用于写入恢复记录
    const char* selectAlertSQL = "SELECT hostname, created_time FROM alerts WHERE ip = ?;";
    sqlite3_stmt* selectStmt;
    int rc = sqlite3_prepare_v2(db.get(), selectAlertSQL, -1, &selectStmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare alert select statement: {}",
                     sqlite3_errmsg(db.get()));
        return false;
    }

    sqlite3_bind_text(selectStmt, 1, ip.c_str(), -1, SQLITE_STATIC);

    std::string hostname, alertTime;
    if (sqlite3_step(selectStmt) == SQLITE_ROW) {
        const char* hostText = (const char*)sqlite3_column_text(selectStmt, 0);
        const char* timeText = (const char*)sqlite3_column_text(selectStmt, 1);
        if (hostText) {
            hostname = hostText;
        }
        if (timeText) {
            alertTime = timeText;
        }
    }
    sqlite3_finalize(selectStmt);

    // 从告警表中删除记录
    const char* deleteAlertSQL = "DELETE FROM alerts WHERE ip = ?;";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db.get(), deleteAlertSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare alert delete statement: {}",
                     sqlite3_errmsg(db.get()));
        return false;
    }

    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::println(std::cerr, "Failed to execute alert delete statement: {}",
                     sqlite3_errmsg(db.get()));
        return false;
    }

    // 如果找到了告警记录，将其写入恢复记录表
    if (!hostname.empty() && !alertTime.empty()) {
        const char* insertRecoverySQL = R"(
            INSERT INTO recovery_records (ip, hostname, alert_time, recovery_time)
            VALUES (?, ?, ?, datetime('now', 'utc'));
        )";

        sqlite3_stmt* insertStmt;
        rc = sqlite3_prepare_v2(db.get(), insertRecoverySQL, -1, &insertStmt, 0);
        if (rc != SQLITE_OK) {
            std::println(std::cerr, "Failed to prepare recovery record insert statement: {}",
                         sqlite3_errmsg(db.get()));
            return false;
        }

        sqlite3_bind_text(insertStmt, 1, ip.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 3, alertTime.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(insertStmt);
        sqlite3_finalize(insertStmt);

        if (rc != SQLITE_DONE) {
            std::println(std::cerr, "Failed to execute recovery record insert statement: {}",
                         sqlite3_errmsg(db.get()));
            return false;
        }
    }

    return true;
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManager::getActiveAlerts(
    int days) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return alerts;
    }

    // 查询活动告警，按创建时间排序
    std::string selectAlertsSQL;
    if (days >= 0) {
        // 查询指定天数内的告警
        selectAlertsSQL = std::format(
            "SELECT ip, hostname, created_time FROM alerts WHERE created_time >= datetime('now', "
            "'-{} days') ORDER BY created_time DESC;",
            days);
    } else {
        // 查询所有告警
        selectAlertsSQL =
            "SELECT ip, hostname, created_time FROM alerts ORDER BY created_time DESC;";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db.get(), selectAlertsSQL.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare alerts query statement: {}",
                     sqlite3_errmsg(db.get()));
        return alerts;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* ip           = (const char*)sqlite3_column_text(stmt, 0);
        const char* hostname     = (const char*)sqlite3_column_text(stmt, 1);
        const char* created_time = (const char*)sqlite3_column_text(stmt, 2);

        if (ip) {
            std::string ipStr(ip);
            std::string hostnameStr = hostname ? hostname : "";
            std::string timeStr     = created_time ? created_time : "";
            alerts.emplace_back(std::move(ipStr), hostnameStr, timeStr);
        }
    }

    sqlite3_finalize(stmt);
    return alerts;
}

std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
DatabaseManager::getRecoveryRecords(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> records;

    if (!db) {
        std::println(std::cerr, "Database not initialized");
        return records;
    }

    // 查询恢复记录，按恢复时间排序
    std::string selectRecordsSQL;
    if (days >= 0) {
        // 查询指定天数内的恢复记录
        selectRecordsSQL = std::format(
            "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records WHERE "
            "recovery_time >= datetime('now', '-{} days') ORDER BY recovery_time DESC;",
            days);
    } else {
        // 查询所有恢复记录
        selectRecordsSQL =
            "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records ORDER BY "
            "recovery_time DESC;";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db.get(), selectRecordsSQL.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::println(std::cerr, "Failed to prepare recovery records query statement: {}",
                     sqlite3_errmsg(db.get()));
        return records;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id                    = sqlite3_column_int(stmt, 0);
        const char* ip            = (const char*)sqlite3_column_text(stmt, 1);
        const char* hostname      = (const char*)sqlite3_column_text(stmt, 2);
        const char* alert_time    = (const char*)sqlite3_column_text(stmt, 3);
        const char* recovery_time = (const char*)sqlite3_column_text(stmt, 4);

        std::string ipStr           = ip ? ip : "";
        std::string hostnameStr     = hostname ? hostname : "";
        std::string alertTimeStr    = alert_time ? alert_time : "";
        std::string recoveryTimeStr = recovery_time ? recovery_time : "";

        records.emplace_back(id, ipStr, hostnameStr, alertTimeStr, recoveryTimeStr);
    }

    sqlite3_finalize(stmt);
    return records;
}