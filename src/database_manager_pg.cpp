#include "database_manager_pg.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <print>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "database_interface.h"

DatabaseManagerPG::DatabaseManagerPG(const std::string& connectionInfo)
    : connInfo(connectionInfo), conn(nullptr) {
    if (connectionInfo.empty()) {
        throw std::invalid_argument("Database connection info cannot be empty");
    }
}

DatabaseManagerPG::~DatabaseManagerPG() {
    if (conn) {
        PQfinish(conn);
    }
}

std::string DatabaseManagerPG::escapeString(const std::string& str) {
    if (!conn) {
        return str;
    }

    char* escaped = PQescapeLiteral(conn, str.c_str(), str.length());
    if (!escaped) {
        return str;
    }

    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

bool DatabaseManagerPG::executeQuery(const std::string& query) {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 检查连接状态，如果连接断开则尝试重新连接
    if (PQstatus(conn) != CONNECTION_OK) {
        std::println(std::cerr, "Database connection lost. Attempting to reconnect...");
        PQfinish(conn);
        conn = PQconnectdb(connInfo.c_str());

        if (PQstatus(conn) != CONNECTION_OK) {
            std::println(std::cerr, "Failed to reconnect to database: {}", PQerrorMessage(conn));
            return false;
        }
    }

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
}

PGresult* DatabaseManagerPG::executeQueryWithResult(const std::string& query) {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return nullptr;
    }

    // 检查连接状态，如果连接断开则尝试重新连接
    if (PQstatus(conn) != CONNECTION_OK) {
        std::println(std::cerr, "Database connection lost. Attempting to reconnect...");
        PQfinish(conn);
        conn = PQconnectdb(connInfo.c_str());

        if (PQstatus(conn) != CONNECTION_OK) {
            std::println(std::cerr, "Failed to reconnect to database: {}", PQerrorMessage(conn));
            return nullptr;
        }
    }

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return nullptr;
    }
    return res;
}

// 检查数据库连接状态
bool DatabaseManagerPG::checkConnection() {
    if (!conn) {
        return false;
    }

    // 使用PQping检查连接状态
    PGPing pingResult = PQping(connInfo.c_str());
    if (pingResult == PQPING_OK) {
        // 连接正常
        return true;
    } else if (pingResult == PQPING_REJECT) {
        // 服务器运行但拒绝连接
        std::println(std::cerr, "Database server is running but rejecting connections");
        return false;
    } else {
        // 服务器未响应，尝试重新连接
        std::println(std::cerr, "Database server is not responding. Attempting to reconnect...");
        PQfinish(conn);
        conn = PQconnectdb(connInfo.c_str());

        if (PQstatus(conn) != CONNECTION_OK) {
            std::println(std::cerr, "Failed to reconnect to database: {}", PQerrorMessage(conn));
            return false;
        }

        return true;
    }
}

bool DatabaseManagerPG::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex);

    conn = PQconnectdb(connInfo.c_str());

    if (PQstatus(conn) != CONNECTION_OK) {
        std::println(std::cerr, "Failed to connect to database: {}", PQerrorMessage(conn));
        return false;
    }

    // 设置client_min_messages参数以抑制NOTICE消息
    // 检查连接字符串中是否包含client_min_messages参数
    if (connInfo.find("client_min_messages") == std::string::npos) {
        PGresult* res = PQexec(conn, "SET client_min_messages TO WARNING;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::println(std::cerr, "Failed to set client_min_messages: {}",
                         PQresultErrorMessage(res));
            PQclear(res);
            return false;
        }
        PQclear(res);
    }

    // 设置连接保持活动状态
    PGresult* res = PQexec(conn, "SET tcp_keepalives_idle = 60;");
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
            created_time TIMESTAMP DEFAULT (now() AT TIME ZONE 'UTC'),
            last_seen TIMESTAMP,
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
            timestamp TIMESTAMP NOT NULL
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
            created_time TIMESTAMP
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
            alert_time TIMESTAMP,
            recovery_time TIMESTAMP
        );
    )";

    if (!executeQuery(createRecoveryRecordsTableSQL)) {
        std::println(std::cerr, "Failed to create recovery_records table");
        return false;
    }

    return true;
}

bool DatabaseManagerPG::insertPingResult(const std::string& ip, const std::string& hostname,
                                         short delay, bool success, const std::string& timestamp) {
    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::cerr << "Invalid IP address format: " << ip << std::endl;
        return false;
    }

    // 检查数据库连接
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "Database not properly initialized or connection lost" << std::endl;
        return false;
    }

    // 创建一个包含单个结果的向量并调用批量插入函数
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results;
    results.emplace_back(ip, hostname, delay, success, timestamp);
    return insertPingResults(results);
}

// 辅助函数：创建IP表和索引（已重构为使用统一表，此函数保持为空以保持接口兼容性）
bool DatabaseManagerPG::createIPTables(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    // 已经在initialize()中创建了统一的ping_results表和索引
    // 此处无需额外操作
    return true;
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
            "VALUES ($1, $2, NOW() AT TIME ZONE 'UTC', $4::boolean, $3::integer) "
            "ON CONFLICT (ip) DO UPDATE SET "
            "hostname = EXCLUDED.hostname, "
            "last_seen = EXCLUDED.last_seen, "
            "last_status = EXCLUDED.last_status, "
            "last_delay = EXCLUDED.last_delay;";

        PGresult* res =
            PQexecParams(conn, insertSQL, 4, nullptr, paramValues, paramLengths, paramFormats, 0);

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

        PGresult* res =
            PQexecParams(conn, insertSQL, 5, nullptr, paramValues, paramLengths, paramFormats, 0);

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
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }

    if (results.empty()) {
        return true;  // 没有结果需要插入，视为成功
    }

    bool success = true;

    // 验证所有IP地址格式
    if (success) {
        success = DatabaseBase::validateIPs(results);
    }

    // 为所有IP地址创建表（如果尚未创建）
    if (success) {
        success = createIPTables(results);
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

    // 使用参数化查询获取主机名
    const char* hostQuerySQL   = "SELECT hostname FROM hosts WHERE ip = $1";
    const char* paramValues[1] = {ip.c_str()};
    int paramLengths[1]        = {static_cast<int>(ip.length())};
    int paramFormats[1]        = {0};

    PGresult* hostRes =
        PQexecParams(conn, hostQuerySQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (!hostRes || PQresultStatus(hostRes) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to query host information");
        if (hostRes) PQclear(hostRes);
        return;
    }

    std::string hostname = "";
    if (PQntuples(hostRes) > 0) {
        char* hostText = PQgetvalue(hostRes, 0, 0);
        if (hostText) {
            hostname = hostText;
        }
    }
    PQclear(hostRes);

    std::println(std::cout, "Statistics for IP: {} ({})", ip, hostname);
    std::println(std::cout, "=========================================================");

    // 使用参数化查询获取统计信息
    const char* statsSQL =
        "SELECT "
        "COUNT(*) as total_records, "
        "COUNT(*) FILTER (WHERE success = true) as success_count, "
        "AVG(delay) FILTER (WHERE success = true) as avg_delay, "
        "MAX(delay) FILTER (WHERE success = true) as max_delay, "
        "MIN(delay) FILTER (WHERE success = true) as min_delay "
        "FROM ping_results WHERE ip = $1";

    PGresult* statsRes =
        PQexecParams(conn, statsSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (!statsRes || PQresultStatus(statsRes) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to query statistics");
        if (statsRes) PQclear(statsRes);
        return;
    }

    if (PQntuples(statsRes) == 0) {
        std::println(std::cout, "No ping records found for this IP.");
        PQclear(statsRes);
        return;
    }

    int totalRecords = std::atoi(PQgetvalue(statsRes, 0, 0));
    int successCount = std::atoi(PQgetvalue(statsRes, 0, 1));
    double avgDelay  = (PQgetvalue(statsRes, 0, 2)) ? std::atof(PQgetvalue(statsRes, 0, 2)) : 0;
    int maxDelay     = (PQgetvalue(statsRes, 0, 3)) ? std::atoi(PQgetvalue(statsRes, 0, 3)) : 0;
    int minDelay     = (PQgetvalue(statsRes, 0, 4)) ? std::atoi(PQgetvalue(statsRes, 0, 4)) : 0;

    PQclear(statsRes);

    std::println(std::cout, "Total ping records: {}", totalRecords);

    if (totalRecords == 0) {
        std::println(std::cout, "No ping records found for this IP.");
        return;
    }

    int failureCount = totalRecords - successCount;
    double successRate =
        (totalRecords > 0) ? static_cast<double>(successCount) / totalRecords * 100 : 0;
    double failureRate =
        (totalRecords > 0) ? static_cast<double>(failureCount) / totalRecords * 100 : 0;

    std::println(std::cout, "Successful pings: {}", successCount);
    std::println(std::cout, "Failed pings: {}", failureCount);
    std::println(std::cout, "Success rate: {:.2f}%", successRate);
    std::println(std::cout, "Failure rate: {:.2f}%", failureRate);
    std::println(std::cout, "Average delay (successful pings): {:.2f}ms", avgDelay);
    std::println(std::cout, "Maximum delay (successful pings): {}ms", maxDelay);
    std::println(std::cout, "Minimum delay (successful pings): {}ms", minDelay);

    // 使用参数化查询获取最近的10条记录
    const char* recentSQL =
        "SELECT delay, success, timestamp FROM ping_results WHERE ip = $1 ORDER BY timestamp DESC "
        "LIMIT 10";

    PGresult* recentRes =
        PQexecParams(conn, recentSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (!recentRes || PQresultStatus(recentRes) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to query recent records");
        if (recentRes) PQclear(recentRes);
        return;
    }

    std::println(std::cout, "\nRecent ping records (last 10):");
    std::println(std::cout, "Timestamp           \tDelay\tStatus");
    std::println(std::cout, "--------------------------------------------------------");

    for (int i = 0; i < PQntuples(recentRes); i++) {
        char* timestamp = PQgetvalue(recentRes, i, 2);
        char* delay     = PQgetvalue(recentRes, i, 0);
        char* success   = PQgetvalue(recentRes, i, 1);

        std::println(std::cout, "{}\t{}ms\t{}", timestamp ? timestamp : "N/A",
                     delay ? delay : "N/A",
                     (success && std::strcmp(success, "t") == 0 ? "Success" : "Failed"));
    }
    PQclear(recentRes);
}

void DatabaseManagerPG::cleanupOldData(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    std::println(std::cout, "Cleaning up data older than {} days...", days);

    // 使用参数化查询从统一的ping_results表中删除指定天数之前的数据
    const char* deleteSQL =
        "DELETE FROM ping_results WHERE timestamp < NOW() - ($1 * INTERVAL '1 day')";
    std::string daysStr        = std::to_string(days);
    const char* paramValues[1] = {daysStr.c_str()};
    int paramLengths[1]        = {static_cast<int>(daysStr.length())};
    int paramFormats[1]        = {0};

    PGresult* deleteRes =
        PQexecParams(conn, deleteSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (PQresultStatus(deleteRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to delete old data: {}", PQresultErrorMessage(deleteRes));
        PQclear(deleteRes);
        return;
    }

    int totalDeleted = std::atoi(PQcmdTuples(deleteRes));
    PQclear(deleteRes);

    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);

    // 使用参数化查询清理hosts表中长时间未见的数据（超过2倍保留天数）
    std::string cleanupDaysStr        = std::to_string(days * 2);
    const char* cleanupParamValues[1] = {cleanupDaysStr.c_str()};
    int cleanupParamLengths[1]        = {static_cast<int>(cleanupDaysStr.length())};

    const char* cleanupHostsSQL =
        "DELETE FROM hosts WHERE last_seen < NOW() - ($1 * INTERVAL '1 day')";
    PGresult* cleanupHostsRes = PQexecParams(conn, cleanupHostsSQL, 1, nullptr, cleanupParamValues,
                                             cleanupParamLengths, paramFormats, 0);

    if (PQresultStatus(cleanupHostsRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to cleanup old hosts: {}",
                     PQresultErrorMessage(cleanupHostsRes));
        PQclear(cleanupHostsRes);
        return;
    }

    int hostsDeleted = std::atoi(PQcmdTuples(cleanupHostsRes));
    PQclear(cleanupHostsRes);

    if (hostsDeleted > 0) {
        std::println(std::cout, "Deleted {} old host records", hostsDeleted);
    }

    std::println(std::cout, "Cleanup completed.");
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
    if (!isValidIP(ip)) {
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
        "VALUES ($1, $2, NOW() AT TIME ZONE 'UTC') "
        "ON CONFLICT (ip) DO NOTHING";

    PGresult* res =
        PQexecParams(conn, insertSQL, 2, nullptr, paramValues, paramLengths, paramFormats, 0);

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
    if (!isValidIP(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 使用参数化查询获取告警信息，用于写入恢复记录
    const char* selectSQL      = "SELECT hostname, created_time FROM alerts WHERE ip = $1";
    const char* paramValues[1] = {ip.c_str()};
    int paramLengths[1]        = {static_cast<int>(ip.length())};
    int paramFormats[1]        = {0};

    PGresult* selectRes =
        PQexecParams(conn, selectSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    if (!selectRes || PQresultStatus(selectRes) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to query alert information for IP: {}", ip);
        if (selectRes) PQclear(selectRes);
        return false;
    }

    std::string hostname, alertTime;
    if (PQntuples(selectRes) > 0) {
        char* hostText = PQgetvalue(selectRes, 0, 0);
        char* timeText = PQgetvalue(selectRes, 0, 1);
        if (hostText) {
            hostname = hostText;
        }
        if (timeText) {
            alertTime = timeText;
        }
    }
    PQclear(selectRes);

    // 使用参数化查询从告警表中删除记录
    const char* deleteSQL = "DELETE FROM alerts WHERE ip = $1";
    PGresult* deleteRes =
        PQexecParams(conn, deleteSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);

    if (!deleteRes || PQresultStatus(deleteRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to remove alert for IP: {}", ip);
        if (deleteRes) PQclear(deleteRes);
        return false;
    }
    PQclear(deleteRes);

    // 如果找到了告警记录，将其写入恢复记录表
    if (!hostname.empty() && !alertTime.empty()) {
        const char* insertRecoveryParamValues[3];
        int insertRecoveryParamLengths[3];
        int insertRecoveryParamFormats[3];

        insertRecoveryParamValues[0]  = ip.c_str();
        insertRecoveryParamValues[1]  = hostname.c_str();
        insertRecoveryParamValues[2]  = alertTime.c_str();
        insertRecoveryParamLengths[0] = static_cast<int>(ip.length());
        insertRecoveryParamLengths[1] = static_cast<int>(hostname.length());
        insertRecoveryParamLengths[2] = static_cast<int>(alertTime.length());
        insertRecoveryParamFormats[0] = 0;
        insertRecoveryParamFormats[1] = 0;
        insertRecoveryParamFormats[2] = 0;

        const char* insertRecoverySQL =
            "INSERT INTO recovery_records (ip, hostname, alert_time, recovery_time) "
            "VALUES ($1, $2, $3, NOW() AT TIME ZONE 'UTC')";

        PGresult* insertRes =
            PQexecParams(conn, insertRecoverySQL, 3, nullptr, insertRecoveryParamValues,
                         insertRecoveryParamLengths, insertRecoveryParamFormats, 0);

        if (!insertRes || PQresultStatus(insertRes) != PGRES_COMMAND_OK) {
            std::println(std::cerr, "Failed to insert recovery record for IP: {}", ip);
            if (insertRes) PQclear(insertRes);
            return false;
        }
        PQclear(insertRes);
    }

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

        res = PQexecParams(conn, selectSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    } else {
        // 查询所有告警
        const char* selectSQL =
            "SELECT ip, hostname, created_time FROM alerts ORDER BY created_time DESC";
        res = PQexec(conn, selectSQL);
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

        res = PQexecParams(conn, selectSQL, 1, nullptr, paramValues, paramLengths, paramFormats, 0);
    } else {
        // 查询所有恢复记录
        const char* selectSQL =
            "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records ORDER BY "
            "recovery_time DESC";
        res = PQexec(conn, selectSQL);
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