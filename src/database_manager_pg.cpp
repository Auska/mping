#include "database_manager_pg.h"

#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <print>
#include <sstream>
#include <stdexcept>

#include "ip_validator.h"

namespace {

// 统计信息与最近记录输出辅助
void printHeader(const std::string& ip, const std::string& hostname) {
    std::println(std::cout, "Statistics for IP: {} ({})", ip, hostname);
    std::println(std::cout, "=========================================================");
}

void printBody(const PingStatistics& stats) {
    std::println(std::cout, "Total ping records: {}", stats.totalRecords);
    if (stats.totalRecords == 0) {
        std::println(std::cout, "No ping records found for this IP.");
        return;
    }
    std::println(std::cout, "Successful pings: {}", stats.successCount);
    std::println(std::cout, "Failed pings: {}", stats.failureCount);
    std::println(std::cout, "Success rate: {:.2f}%", stats.successRate);
    std::println(std::cout, "Failure rate: {:.2f}%", stats.failureRate);
    std::println(std::cout, "Average delay (successful pings): {:.2f}ms", stats.avgDelay);
    std::println(std::cout, "Maximum delay (successful pings): {}ms", stats.maxDelay);
    std::println(std::cout, "Minimum delay (successful pings): {}ms", stats.minDelay);
}

void printRecentRecordsHeader() {
    std::println(std::cout, "\nRecent ping records (last 10):");
    std::println(std::cout, "Timestamp           \tDelay\tStatus");
    std::println(std::cout, "--------------------------------------------------------");
}

void printRecentRecordRow(const std::string& timestamp, int delay, bool success) {
    std::println(std::cout, "{}\t{}ms\t{}", timestamp, delay, success ? "Success" : "Failed");
}

}  // namespace

DatabaseManagerPG::DatabaseManagerPG(const std::string& connectionInfo)
    : connInfo(connectionInfo), conn(nullptr) {
    if (connectionInfo.empty()) {
        throw std::invalid_argument("Database connection info cannot be empty");
    }
}

bool DatabaseManagerPG::ensureConnected() {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    if (PQstatus(conn.get()) == CONNECTION_OK) {
        return true;
    }

    // 连接断开则尝试重新连接
    std::println(std::cerr, "Database connection lost. Attempting to reconnect...");
    PQfinish(conn.get());
    PGconn* rawConn = PQconnectdb(connInfo.c_str());
    conn.reset(rawConn);

    if (PQstatus(conn.get()) != CONNECTION_OK) {
        std::println(std::cerr, "Failed to reconnect to database: {}", PQerrorMessage(conn.get()));
        return false;
    }
    return true;
}

bool DatabaseManagerPG::executeQuery(const std::string& query) {
    if (!ensureConnected()) {
        return false;
    }

    PGresult* res = PQexec(conn.get(), query.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
}

PGresult* DatabaseManagerPG::executeQueryWithResult(const std::string& query) {
    if (!ensureConnected()) {
        return nullptr;
    }

    PGresult* res = PQexec(conn.get(), query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return nullptr;
    }
    return res;
}

bool DatabaseManagerPG::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex);

    PGconn* rawConn = PQconnectdb(connInfo.c_str());
    conn.reset(rawConn);

    if (PQstatus(conn.get()) != CONNECTION_OK) {
        std::println(std::cerr, "Failed to connect to database: {}", PQerrorMessage(conn.get()));
        return false;
    }

    // 设置client_min_messages参数以抑制NOTICE消息
    // 检查连接字符串中是否包含client_min_messages参数
    if (connInfo.find("client_min_messages") == std::string::npos) {
        PGresult* res = PQexec(conn.get(), "SET client_min_messages TO WARNING;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::println(std::cerr, "Failed to set client_min_messages: {}",
                         PQresultErrorMessage(res));
            PQclear(res);
            return false;
        }
        PQclear(res);
    }

    // 会话统一按 UTC 解释时间戳：ping_results 的时间以 UTC 文本写入，
    // 必须固定会话时区，否则会受服务器默认时区影响而产生偏移（与迁移逻辑一致）
    PGresult* tzRes = PQexec(conn.get(), "SET TIME ZONE 'UTC';");
    if (PQresultStatus(tzRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to set timezone to UTC: {}", PQresultErrorMessage(tzRes));
        PQclear(tzRes);
        return false;
    }
    PQclear(tzRes);

    // 设置连接保持活动状态
    PGresult* res = PQexec(conn.get(), "SET tcp_keepalives_idle = 60;");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Warning: Failed to set tcp_keepalives_idle: {}",
                     PQresultErrorMessage(res));
    }
    PQclear(res);

    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMPTZ DEFAULT NOW(),
            last_seen TIMESTAMPTZ,
            last_status BOOLEAN,
            last_delay INTEGER
        );
    )";

    if (!executeQuery(createHostsTableSQL)) {
        std::println(std::cerr, "Failed to create hosts table");
        return false;
    }

    // 创建ping_results表，用于存储所有ping结果
    const char* createPingResultsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS ping_results (
            id SERIAL PRIMARY KEY,
            ip TEXT NOT NULL,
            hostname TEXT,
            delay INTEGER,
            success BOOLEAN,
            timestamp TIMESTAMPTZ NOT NULL
        );
    )";

    if (!executeQuery(createPingResultsTableSQL)) {
        std::println(std::cerr, "Failed to create ping_results table");
        return false;
    }

    // 为ping_results表的ip和timestamp列创建索引以提高查询性能
    const char* createPingResultsIndexSQL = R"(
        CREATE INDEX IF NOT EXISTS idx_ping_results_ip ON ping_results (ip);
        CREATE INDEX IF NOT EXISTS idx_ping_results_timestamp ON ping_results (timestamp);
    )";

    if (!executeQuery(createPingResultsIndexSQL)) {
        std::println(std::cerr, "Failed to create indexes for ping_results table");
        return false;
    }

    // 创建alerts表，用于存储告警信息
    const char* createAlertsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS alerts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMPTZ
        );
    )";

    if (!executeQuery(createAlertsTableSQL)) {
        std::println(std::cerr, "Failed to create alerts table");
        return false;
    }

    // 创建recovery_records表，用于存储恢复记录
    const char* createRecoveryRecordsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS recovery_records (
            id SERIAL PRIMARY KEY,
            ip TEXT,
            hostname TEXT,
            alert_time TIMESTAMPTZ,
            recovery_time TIMESTAMPTZ
        );
    )";

    if (!executeQuery(createRecoveryRecordsTableSQL)) {
        std::println(std::cerr, "Failed to create recovery_records table");
        return false;
    }

    // 迁移旧 TIMESTAMP 列为 TIMESTAMPTZ
    if (!migrateSchema()) {
        std::println(std::cerr, "Warning: Schema migration incomplete, continuing anyway");
    }

    return true;
}

bool DatabaseManagerPG::migrateSchema() {
    // 确保会话时区为 UTC，这样现有 TIMESTAMP 数据会被正确解释为 UTC 时间
    if (!executeQuery("SET TIME ZONE 'UTC';")) {
        std::println(std::cerr, "Failed to set timezone to UTC for migration");
        return false;
    }

    // 查询所有需要迁移的列：类型为 timestamp without time zone 的列
    const char* checkSQL = R"(
        SELECT table_name, column_name
        FROM information_schema.columns
        WHERE table_schema = 'public'
          AND data_type = 'timestamp without time zone'
          AND table_name IN ('hosts', 'ping_results', 'alerts', 'recovery_records')
        ORDER BY table_name, column_name;
    )";

    PGresult* res = executeQueryWithResult(checkSQL);
    if (!res) {
        std::println(std::cerr, "Failed to check schema for migration");
        return false;
    }

    int colCount = PQntuples(res);
    if (colCount == 0) {
        PQclear(res);
        return true;  // 没有需要迁移的列
    }

    std::println(std::cout, "Migrating {} timestamp column(s) to timestamptz...", colCount);

    for (int i = 0; i < colCount; i++) {
        std::string table  = PQgetvalue(res, i, 0);
        std::string column = PQgetvalue(res, i, 1);

        std::string alterSQL =
            "ALTER TABLE " + table + " ALTER COLUMN " + column + " TYPE TIMESTAMPTZ";
        std::println(std::cout, "  Migrating {}.{}...", table, column);

        if (!executeQuery(alterSQL)) {
            std::println(std::cerr, "Failed to migrate {}.{} to TIMESTAMPTZ", table, column);
            PQclear(res);
            return false;
        }
    }

    PQclear(res);
    std::println(std::cout, "Schema migration completed.");
    return true;
}

bool DatabaseManagerPG::insertPingResult(const std::string& ip, const std::string& hostname,
                                         short delay, bool success, const std::string& timestamp) {
    // 验证IP地址格式
    if (!IPValidator::isValidIPv4(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 检查数据库连接
    if (!conn || PQstatus(conn.get()) != CONNECTION_OK) {
        std::println(std::cerr, "Database not properly initialized or connection lost");
        return false;
    }

    // 创建一个包含单个结果的向量并调用批量插入函数
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results;
    results.emplace_back(ip, hostname, delay, success, timestamp);
    return insertPingResults(results);
}

// 辅助函数：批量插入主机信息
bool DatabaseManagerPG::insertHostsBatch(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (results.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    // 开始事务以提高性能
    if (!executeQuery("BEGIN;")) {
        std::println(std::cerr, "Failed to begin transaction for hosts");
        return false;
    }

    // 使用参数化查询逐个插入，防止SQL注入
    const char* paramValues[4];
    int paramLengths[4];
    int paramFormats[4];

    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        // 设置参数值
        paramValues[0]        = ip.c_str();
        paramValues[1]        = hostname.c_str();
        std::string delayStr  = std::to_string(delay);
        paramValues[2]        = delayStr.c_str();
        std::string statusStr = successFlag ? "true" : "false";
        paramValues[3]        = statusStr.c_str();

        // 设置参数长度
        paramLengths[0] = static_cast<int>(ip.length());
        paramLengths[1] = static_cast<int>(hostname.length());
        paramLengths[2] = static_cast<int>(delayStr.length());
        paramLengths[3] = static_cast<int>(statusStr.length());

        // 设置参数格式（0表示文本格式）
        paramFormats[0] = 0;
        paramFormats[1] = 0;
        paramFormats[2] = 0;
        paramFormats[3] = 0;

        // 使用参数化查询
        const char* insertSQL =
            "INSERT INTO hosts (ip, hostname, last_seen, last_status, last_delay) "
            "VALUES ($1, $2, NOW(), $4::boolean, $3::integer) "
            "ON CONFLICT (ip) DO UPDATE SET "
            "hostname = EXCLUDED.hostname, "
            "last_seen = EXCLUDED.last_seen, "
            "last_status = EXCLUDED.last_status, "
            "last_delay = EXCLUDED.last_delay;";

        PGresult* res = PQexecParams(conn.get(), insertSQL, 4, nullptr, paramValues, paramLengths,
                                     paramFormats, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::println(std::cerr, "Failed to insert host record for IP {}: {}", ip,
                         PQresultErrorMessage(res));
            PQclear(res);
            executeQuery("ROLLBACK;");
            return false;
        }
        PQclear(res);
    }

    // 提交事务
    if (!executeQuery("COMMIT;")) {
        std::println(std::cerr, "Failed to commit transaction for hosts");
        return false;
    }

    return true;
}

// 辅助函数：批量插入ping结果
bool DatabaseManagerPG::insertPingResultsBatch(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (results.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    // 开始事务以提高性能
    if (!executeQuery("BEGIN;")) {
        std::println(std::cerr, "Failed to begin transaction for ping results");
        return false;
    }

    // 使用参数化查询逐个插入，防止SQL注入
    const char* paramValues[5];
    int paramLengths[5];
    int paramFormats[5];

    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        // 设置参数值
        paramValues[0]        = ip.c_str();
        paramValues[1]        = hostname.c_str();
        std::string delayStr  = std::to_string(delay);
        paramValues[2]        = delayStr.c_str();
        std::string statusStr = successFlag ? "true" : "false";
        paramValues[3]        = statusStr.c_str();
        paramValues[4]        = timestamp.c_str();

        // 设置参数长度
        paramLengths[0] = static_cast<int>(ip.length());
        paramLengths[1] = static_cast<int>(hostname.length());
        paramLengths[2] = static_cast<int>(delayStr.length());
        paramLengths[3] = static_cast<int>(statusStr.length());
        paramLengths[4] = static_cast<int>(timestamp.length());

        // 设置参数格式（0表示文本格式）
        paramFormats[0] = 0;
        paramFormats[1] = 0;
        paramFormats[2] = 0;
        paramFormats[3] = 0;
        paramFormats[4] = 0;

        // 使用参数化查询
        const char* insertSQL =
            "INSERT INTO ping_results (ip, hostname, delay, success, timestamp) "
            "VALUES ($1, $2, $3::integer, $4::boolean, $5)";

        PGresult* res = PQexecParams(conn.get(), insertSQL, 5, nullptr, paramValues, paramLengths,
                                     paramFormats, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::println(std::cerr, "Failed to insert ping result for IP {}: {}", ip,
                         PQresultErrorMessage(res));
            PQclear(res);
            executeQuery("ROLLBACK;");
            return false;
        }
        PQclear(res);
    }

    // 提交事务
    if (!executeQuery("COMMIT;")) {
        std::println(std::cerr, "Failed to commit transaction for ping results");
        return false;
    }

    return true;
}

bool DatabaseManagerPG::insertPingResults(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    if (results.empty()) {
        return true;  // 没有结果需要插入，视为成功
    }

    bool success = true;

    // 验证所有IP地址格式
    if (success) {
        for (const auto& result : results) {
            if (!IPValidator::isValidIPv4(std::get<0>(result))) {
                std::println(std::cerr, "Invalid IP address format: {}", std::get<0>(result));
                success = false;
                break;
            }
        }
    }

    // 在hosts表中批量插入或更新IP与主机名的映射关系
    if (success) {
        success = insertHostsBatch(results);
    }

    // 批量插入ping结果
    if (success) {
        success = insertPingResultsBatch(results);
    }

    return success;
}

void DatabaseManagerPG::queryIPStatistics(const std::string& ip) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }
    printHeader(ip, queryHostName(ip));

    auto stats = queryStatistics(ip);
    printBody(stats);

    if (stats.totalRecords > 0) {
        printRecentRecords(ip);
    }
}

std::string DatabaseManagerPG::queryHostName(const std::string& ip) {
    const char* sql     = "SELECT hostname FROM hosts WHERE ip = $1";
    const char* vals[1] = {ip.c_str()};
    int lens[1]         = {static_cast<int>(ip.length())};
    int fmts[1]         = {0};

    PGresult* res = PQexecParams(conn.get(), sql, 1, nullptr, vals, lens, fmts, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return {};
    }

    std::string hostname;
    if (PQntuples(res) > 0 && PQgetvalue(res, 0, 0)) {
        hostname = PQgetvalue(res, 0, 0);
    }
    PQclear(res);
    return hostname;
}

PingStatistics DatabaseManagerPG::queryStatistics(const std::string& ip) {
    PingStatistics stats;
    const char* sql =
        "SELECT COUNT(*), COUNT(*) FILTER (WHERE success = true), "
        "AVG(delay) FILTER (WHERE success = true), "
        "MAX(delay) FILTER (WHERE success = true), "
        "MIN(delay) FILTER (WHERE success = true) "
        "FROM ping_results WHERE ip = $1";
    const char* vals[1] = {ip.c_str()};
    int lens[1]         = {static_cast<int>(ip.length())};
    int fmts[1]         = {0};

    PGresult* res = PQexecParams(conn.get(), sql, 1, nullptr, vals, lens, fmts, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return stats;
    }

    if (PQntuples(res) > 0) {
        stats.totalRecords = std::atoi(PQgetvalue(res, 0, 0));
        stats.successCount = std::atoi(PQgetvalue(res, 0, 1));
        stats.avgDelay     = PQgetisnull(res, 0, 2) ? 0.0 : std::atof(PQgetvalue(res, 0, 2));
        stats.maxDelay     = PQgetisnull(res, 0, 3) ? 0 : std::atoi(PQgetvalue(res, 0, 3));
        stats.minDelay     = PQgetisnull(res, 0, 4) ? 0 : std::atoi(PQgetvalue(res, 0, 4));
        stats.failureCount = stats.totalRecords - stats.successCount;
        if (stats.totalRecords > 0) {
            stats.successRate = (double)stats.successCount / stats.totalRecords * 100;
            stats.failureRate = (double)stats.failureCount / stats.totalRecords * 100;
        }
    }
    PQclear(res);
    return stats;
}

void DatabaseManagerPG::printRecentRecords(const std::string& ip) {
    const char* sql =
        "SELECT delay, success, timestamp FROM ping_results WHERE ip = $1 "
        "ORDER BY timestamp DESC LIMIT 10";
    const char* vals[1] = {ip.c_str()};
    int lens[1]         = {static_cast<int>(ip.length())};
    int fmts[1]         = {0};

    PGresult* res = PQexecParams(conn.get(), sql, 1, nullptr, vals, lens, fmts, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to query recent records");
        if (res) PQclear(res);
        return;
    }

    printRecentRecordsHeader();
    for (int i = 0; i < PQntuples(res); i++) {
        char* ts = PQgetvalue(res, i, 2);
        char* d  = PQgetvalue(res, i, 0);
        char* s  = PQgetvalue(res, i, 1);
        printRecentRecordRow(ts ? ts : "N/A", d ? std::atoi(d) : 0, s && std::strcmp(s, "t") == 0);
    }
    PQclear(res);
}

// 删除 ping_results 表中超过 days 天的旧记录；返回删除行数，失败返回 -1
int DatabaseManagerPG::deleteOldPingResults(int days) {
    const char* deleteSQL =
        "DELETE FROM ping_results WHERE timestamp < NOW() - ($1 * INTERVAL '1 day')";
    std::string daysStr        = std::to_string(days);
    const char* paramValues[1] = {daysStr.c_str()};
    int paramLengths[1]        = {static_cast<int>(daysStr.length())};
    int paramFormats[1]        = {0};

    PGresult* res =
        PQexecParams(conn.get(), deleteSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to delete old ping results: {}", PQresultErrorMessage(res));
        PQclear(res);
        return -1;
    }

    int deleted = std::atoi(PQcmdTuples(res));
    PQclear(res);
    return deleted;
}

void DatabaseManagerPG::cleanupOldData(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    std::println(std::cout, "Cleaning up data older than {} days...", days);

    int totalDeleted = deleteOldPingResults(days);
    if (totalDeleted < 0) {
        return;
    }
    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);

    // 清理alerts表中超过指定天数的告警记录
    const char* cleanupAlertsSQL =
        "DELETE FROM alerts WHERE created_time < NOW() - ($1 * INTERVAL '1 day')";
    std::string daysStr        = std::to_string(days);
    const char* paramValues[1] = {daysStr.c_str()};
    int paramLengths[1]        = {static_cast<int>(daysStr.length())};
    int paramFormats[1]        = {0};

    PGresult* cleanupAlertsRes = PQexecParams(conn.get(), cleanupAlertsSQL, 1, nullptr, paramValues,
                                              paramLengths, paramFormats, 0);

    if (PQresultStatus(cleanupAlertsRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to cleanup old alerts: {}",
                     PQresultErrorMessage(cleanupAlertsRes));
        PQclear(cleanupAlertsRes);
        return;
    }

    int alertsDeleted = std::atoi(PQcmdTuples(cleanupAlertsRes));
    PQclear(cleanupAlertsRes);

    if (alertsDeleted > 0) {
        std::println(std::cout, "Deleted {} old alert records", alertsDeleted);
    }

    // 清理recovery_records表中超过指定天数的恢复记录
    const char* cleanupRecoverySQL =
        "DELETE FROM recovery_records WHERE recovery_time < NOW() - ($1 * INTERVAL '1 day')";
    PGresult* cleanupRecoveryRes = PQexecParams(conn.get(), cleanupRecoverySQL, 1, nullptr,
                                                paramValues, paramLengths, paramFormats, 0);

    if (PQresultStatus(cleanupRecoveryRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to cleanup old recovery records: {}",
                     PQresultErrorMessage(cleanupRecoveryRes));
        PQclear(cleanupRecoveryRes);
        return;
    }

    int recoveryDeleted = std::atoi(PQcmdTuples(cleanupRecoveryRes));
    PQclear(cleanupRecoveryRes);

    if (recoveryDeleted > 0) {
        std::println(std::cout, "Deleted {} old recovery records", recoveryDeleted);
    }

    std::println(std::cout, "Cleanup completed.");
}

void DatabaseManagerPG::cleanupOldPingResults(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    std::println(std::cout, "Cleaning up ping_results older than {} days...", days);

    int totalDeleted = deleteOldPingResults(days);
    if (totalDeleted < 0) {
        return;
    }
    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);
}

std::map<std::string, std::string> DatabaseManagerPG::getAllHosts() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::map<std::string, std::string> hosts;

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return hosts;
    }

    // 查询所有主机，按IP排序以提高查询效率
    PGresult* res = executeQueryWithResult("SELECT ip, hostname FROM hosts ORDER BY ip;");
    if (!res) {
        std::println(std::cerr, "Failed to query hosts");
        return hosts;
    }

    for (int row = 0; row < PQntuples(res); row++) {
        char* ip       = PQgetvalue(res, row, 0);
        char* hostname = PQgetvalue(res, row, 1);

        if (ip) {
            std::string ipStr       = ip;
            std::string hostnameStr = hostname ? hostname : "";
            hosts[ipStr]            = hostnameStr;
        }
    }

    PQclear(res);
    return hosts;
}

bool DatabaseManagerPG::addAlert(const std::string& ip, const std::string& hostname) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 验证IP地址格式
    if (!IPValidator::isValidIPv4(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 使用参数化查询插入或更新告警记录
    const char* paramValues[2];
    int paramLengths[2];
    int paramFormats[2];

    paramValues[0]  = ip.c_str();
    paramValues[1]  = hostname.c_str();
    paramLengths[0] = static_cast<int>(ip.length());
    paramLengths[1] = static_cast<int>(hostname.length());
    paramFormats[0] = 0;
    paramFormats[1] = 0;

    const char* insertSQL =
        "INSERT INTO alerts (ip, hostname, created_time) "
        "VALUES ($1, $2, NOW()) "
        "ON CONFLICT (ip) DO NOTHING";

    PGresult* res =
        PQexecParams(conn.get(), insertSQL, 2, nullptr, paramValues, paramLengths, paramFormats, 0);

    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to add alert for IP {}: {}", ip,
                     res ? PQresultErrorMessage(res) : "Unknown error");
        if (res) PQclear(res);
        return false;
    }

    PQclear(res);
    return true;
}

bool DatabaseManagerPG::removeAlert(const std::string& ip) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 验证IP地址格式
    if (!IPValidator::isValidIPv4(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 单条语句原子完成：删除告警并把被删告警写入恢复记录。
    // 告警不存在时无行删除也不产生恢复记录，且中途失败不会丢失恢复数据。
    const char* sql =
        "WITH gone AS (DELETE FROM alerts WHERE ip = $1 RETURNING hostname, created_time) "
        "INSERT INTO recovery_records (ip, hostname, alert_time, recovery_time) "
        "SELECT $1, hostname, created_time, NOW() FROM gone";
    const char* paramValues[1] = {ip.c_str()};
    int paramLengths[1]        = {static_cast<int>(ip.length())};
    int paramFormats[1]        = {0};

    PGresult* res =
        PQexecParams(conn.get(), sql, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to remove alert for IP {}: {}", ip,
                     res ? PQresultErrorMessage(res) : "Unknown error");
        if (res) PQclear(res);
        return false;
    }
    PQclear(res);

    return true;
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManagerPG::getActiveAlerts(
    int days) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return alerts;
    }

    PGresult* res = nullptr;

    // 查询活动告警，按创建时间排序
    if (days >= 0) {
        // 使用参数化查询指定天数内的告警
        const char* selectSQL =
            "SELECT ip, hostname, created_time FROM alerts WHERE created_time >= NOW() - ($1 * "
            "INTERVAL '1 day') ORDER BY created_time DESC";
        std::string daysStr        = std::to_string(days);
        const char* paramValues[1] = {daysStr.c_str()};
        int paramLengths[1]        = {static_cast<int>(daysStr.length())};
        int paramFormats[1]        = {0};

        res = PQexecParams(conn.get(), selectSQL, 1, nullptr, paramValues, paramLengths,
                           paramFormats, 0);
    } else {
        // 查询所有告警
        const char* selectSQL =
            "SELECT ip, hostname, created_time FROM alerts ORDER BY created_time DESC";
        res = PQexec(conn.get(), selectSQL);
    }

    if (!res
        || (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK)) {
        std::println(std::cerr, "Failed to query alerts");
        if (res) PQclear(res);
        return alerts;
    }

    // 预分配空间以提高性能
    alerts.reserve(PQntuples(res));

    for (int row = 0; row < PQntuples(res); row++) {
        char* ip           = PQgetvalue(res, row, 0);
        char* hostname     = PQgetvalue(res, row, 1);
        char* created_time = PQgetvalue(res, row, 2);

        if (ip) {
            std::string ipStr       = ip ? ip : "";
            std::string hostnameStr = hostname ? hostname : "";
            std::string timeStr     = created_time ? created_time : "";
            alerts.emplace_back(ipStr, hostnameStr, timeStr);
        }
    }

    PQclear(res);
    return alerts;
}

std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
DatabaseManagerPG::getRecoveryRecords(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> records;

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return records;
    }

    PGresult* res = nullptr;

    // 查询恢复记录，按恢复时间排序
    if (days >= 0) {
        // 使用参数化查询指定天数内的恢复记录
        const char* selectSQL =
            "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records WHERE "
            "recovery_time >= NOW() - ($1 * INTERVAL '1 day') ORDER BY recovery_time DESC";
        std::string daysStr        = std::to_string(days);
        const char* paramValues[1] = {daysStr.c_str()};
        int paramLengths[1]        = {static_cast<int>(daysStr.length())};
        int paramFormats[1]        = {0};

        res = PQexecParams(conn.get(), selectSQL, 1, nullptr, paramValues, paramLengths,
                           paramFormats, 0);
    } else {
        // 查询所有恢复记录
        const char* selectSQL =
            "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records ORDER BY "
            "recovery_time DESC";
        res = PQexec(conn.get(), selectSQL);
    }

    if (!res
        || (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK)) {
        std::println(std::cerr, "Failed to query recovery records");
        if (res) PQclear(res);
        return records;
    }

    // 预分配空间以提高性能
    records.reserve(PQntuples(res));

    for (int row = 0; row < PQntuples(res); row++) {
        int id              = std::atoi(PQgetvalue(res, row, 0));
        char* ip            = PQgetvalue(res, row, 1);
        char* hostname      = PQgetvalue(res, row, 2);
        char* alert_time    = PQgetvalue(res, row, 3);
        char* recovery_time = PQgetvalue(res, row, 4);

        std::string ipStr           = ip ? ip : "";
        std::string hostnameStr     = hostname ? hostname : "";
        std::string alertTimeStr    = alert_time ? alert_time : "";
        std::string recoveryTimeStr = recovery_time ? recovery_time : "";

        records.emplace_back(id, ipStr, hostnameStr, alertTimeStr, recoveryTimeStr);
    }

    PQclear(res);
    return records;
}