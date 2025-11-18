#include "database_manager_pg.h"
#include "database_interface.h"
#include <iostream>
#include <print>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <regex>
#include <stdexcept>
#include <cstring>

DatabaseManagerPG::DatabaseManagerPG(const std::string& connectionInfo) : connInfo(connectionInfo), conn(nullptr) {
    if (connectionInfo.empty()) {
        throw std::invalid_argument("Database connection info cannot be empty");
    }
}

DatabaseManagerPG::~DatabaseManagerPG() {
    if (conn) {
        PQfinish(conn);
    }
}

bool DatabaseManagerPG::isValidIP(const std::string& ip) {
    // 使用正则表达式验证IPv4地址格式
    std::regex ipPattern(R"(^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)");
    return std::regex_match(ip, ipPattern);
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
            std::println(std::cerr, "Failed to set client_min_messages: {}", PQresultErrorMessage(res));
            PQclear(res);
            return false;
        }
        PQclear(res);
    }
    
    // 设置连接保持活动状态
    PGresult* res = PQexec(conn, "SET tcp_keepalives_idle = 60;");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Warning: Failed to set tcp_keepalives_idle: {}", PQresultErrorMessage(res));
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

bool DatabaseManagerPG::insertPingResult(const std::string& ip, const std::string& hostname, short delay, bool success, const std::string& timestamp) {
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

// 辅助函数：验证IP地址格式
bool DatabaseManagerPG::validateIPs(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        if (!isValidIP(ip)) {
            std::cerr << "Invalid IP address format: " << ip << std::endl;
            return false;
        }
    }
    return true;
}

// 辅助函数：创建IP表和索引（已重构为使用统一表，此函数保持为空以保持接口兼容性）
bool DatabaseManagerPG::createIPTables(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    // 已经在initialize()中创建了统一的ping_results表和索引
    // 此处无需额外操作
    return true;
}

// 辅助函数：批量插入主机信息
bool DatabaseManagerPG::insertHostsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (results.empty()) {
        return true;
    }
    
    // 使用批量插入优化性能
    std::ostringstream hostSQLStream;
    hostSQLStream << "INSERT INTO hosts (ip, hostname, last_seen, last_status, last_delay) VALUES ";
    
    bool first = true;
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        if (!first) hostSQLStream << ", ";
        hostSQLStream << "(" << escapeString(ip) << ", " << escapeString(hostname) << ", NOW() AT TIME ZONE 'UTC', "
                      << (successFlag ? "true" : "false") << ", " << delay << ")";
        first = false;
    }
    
    hostSQLStream << " ON CONFLICT (ip) DO UPDATE SET "
                  << "hostname = EXCLUDED.hostname, "
                  << "last_seen = EXCLUDED.last_seen, "
                  << "last_status = EXCLUDED.last_status, "
                  << "last_delay = EXCLUDED.last_delay;";
    
    // 开始事务以提高性能
    if (!executeQuery("BEGIN;")) {
        std::cerr << "Failed to begin transaction for hosts" << std::endl;
        return false;
    }
    
    bool success = executeQuery(hostSQLStream.str());
    
    // 提交或回滚事务
    if (success) {
        if (!executeQuery("COMMIT;")) {
            std::cerr << "Failed to commit transaction for hosts" << std::endl;
            success = false;
        }
    } else {
        executeQuery("ROLLBACK;");
    }
    
    return success;
}

// 辅助函数：批量插入ping结果
bool DatabaseManagerPG::insertPingResultsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (results.empty()) {
        return true;
    }
    
    // 构建批量插入语句到统一的ping_results表
    std::ostringstream batchInsertSQLStream;
    batchInsertSQLStream << "INSERT INTO ping_results (ip, hostname, delay, success, timestamp) VALUES ";
    
    bool first = true;
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        if (!first) batchInsertSQLStream << ", ";
        batchInsertSQLStream << "(" << escapeString(ip) << ", " << escapeString(hostname) << ", " 
                             << delay << ", " << (successFlag ? "true" : "false") << ", " 
                             << escapeString(timestamp) << ")";
        first = false;
    }
    batchInsertSQLStream << ";";
    
    // 开始事务以提高性能
    if (!executeQuery("BEGIN;")) {
        std::cerr << "Failed to begin transaction for ping results" << std::endl;
        return false;
    }
    
    bool success = executeQuery(batchInsertSQLStream.str());
    
    // 提交或回滚事务
    if (success) {
        if (!executeQuery("COMMIT;")) {
            std::cerr << "Failed to commit transaction for ping results" << std::endl;
            success = false;
        }
    } else {
        executeQuery("ROLLBACK;");
    }
    
    return success;
}

bool DatabaseManagerPG::insertPingResults(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    if (results.empty()) {
        return true; // 没有结果需要插入，视为成功
    }
    
    bool success = true;
    
    // 验证所有IP地址格式
    if (success) {
        success = validateIPs(results);
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
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return;
    }
    
    // 获取主机名
    std::ostringstream hostQueryStream;
    hostQueryStream << "SELECT hostname FROM hosts WHERE ip = " << escapeString(ip) << ";";
    
    PGresult* hostRes = executeQueryWithResult(hostQueryStream.str());
    if (!hostRes) {
        std::cerr << "Failed to query host information" << std::endl;
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
    
    std::cout << "Statistics for IP: " << ip << " (" << hostname << ")" << std::endl;
    std::cout << "=========================================================" << std::endl;
    
    // 使用单个查询获取所有统计信息
    std::ostringstream statsSQLStream;
    statsSQLStream << "SELECT "
                   << "COUNT(*) as total_records, "
                   << "COUNT(*) FILTER (WHERE success = true) as success_count, "
                   << "AVG(delay) FILTER (WHERE success = true) as avg_delay, "
                   << "MAX(delay) FILTER (WHERE success = true) as max_delay, "
                   << "MIN(delay) FILTER (WHERE success = true) as min_delay "
                   << "FROM ping_results WHERE ip = " << escapeString(ip) << ";";
    
    PGresult* statsRes = executeQueryWithResult(statsSQLStream.str());
    if (!statsRes) {
        std::cerr << "Failed to query statistics" << std::endl;
        return;
    }
    
    if (PQntuples(statsRes) == 0) {
        std::cout << "No ping records found for this IP." << std::endl;
        PQclear(statsRes);
        return;
    }
    
    int totalRecords = atoi(PQgetvalue(statsRes, 0, 0));
    int successCount = atoi(PQgetvalue(statsRes, 0, 1));
    double avgDelay = (PQgetvalue(statsRes, 0, 2)) ? atof(PQgetvalue(statsRes, 0, 2)) : 0;
    int maxDelay = (PQgetvalue(statsRes, 0, 3)) ? atoi(PQgetvalue(statsRes, 0, 3)) : 0;
    int minDelay = (PQgetvalue(statsRes, 0, 4)) ? atoi(PQgetvalue(statsRes, 0, 4)) : 0;
    
    PQclear(statsRes);
    
    std::cout << "Total ping records: " << totalRecords << std::endl;
    
    if (totalRecords == 0) {
        std::cout << "No ping records found for this IP." << std::endl;
        return;
    }
    
    int failureCount = totalRecords - successCount;
    double successRate = (totalRecords > 0) ? (double)successCount / totalRecords * 100 : 0;
    double failureRate = (totalRecords > 0) ? (double)failureCount / totalRecords * 100 : 0;
    
    std::cout << "Successful pings: " << successCount << std::endl;
    std::cout << "Failed pings: " << failureCount << std::endl;
    std::cout << "Success rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
    std::cout << "Failure rate: " << std::fixed << std::setprecision(2) << failureRate << "%" << std::endl;
    std::cout << "Average delay (successful pings): " << std::fixed << std::setprecision(2) << avgDelay << "ms" << std::endl;
    std::cout << "Maximum delay (successful pings): " << maxDelay << "ms" << std::endl;
    std::cout << "Minimum delay (successful pings): " << minDelay << "ms" << std::endl;
    
    // 显示最近的10条记录
    std::ostringstream recentSQLStream;
    recentSQLStream << "SELECT delay, success, timestamp FROM ping_results WHERE ip = " 
                    << escapeString(ip) << " ORDER BY timestamp DESC LIMIT 10;";
    
    PGresult* recentRes = executeQueryWithResult(recentSQLStream.str());
    if (!recentRes) {
        std::cerr << "Failed to query recent records" << std::endl;
        return;
    }
    
    std::cout << "\nRecent ping records (last 10):" << std::endl;
    std::cout << "Timestamp           \tDelay\tStatus" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    
    for (int i = 0; i < PQntuples(recentRes); i++) {
        char* timestamp = PQgetvalue(recentRes, i, 2);
        char* delay = PQgetvalue(recentRes, i, 0);
        char* success = PQgetvalue(recentRes, i, 1);
        
        std::cout << (timestamp ? timestamp : "N/A") << "\t" 
                  << (delay ? delay : "N/A") << "ms\t" 
                  << (success && strcmp(success, "t") == 0 ? "Success" : "Failed") << std::endl;
    }
    PQclear(recentRes);
}

void DatabaseManagerPG::cleanupOldData(int days) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return;
    }
    
    std::cout << "Cleaning up data older than " << days << " days..." << std::endl;
    
    // 从统一的ping_results表中删除指定天数之前的数据
    std::ostringstream deleteSQLStream;
    deleteSQLStream << "DELETE FROM ping_results WHERE timestamp < NOW() - INTERVAL '" << days << " days';";
    
    PGresult* deleteRes = PQexec(conn, deleteSQLStream.str().c_str());
    if (PQresultStatus(deleteRes) != PGRES_COMMAND_OK) {
        std::cerr << "Failed to delete old data: " << PQresultErrorMessage(deleteRes) << std::endl;
        PQclear(deleteRes);
        return;
    }
    
    int totalDeleted = atoi(PQcmdTuples(deleteRes));
    PQclear(deleteRes);
    
    std::cout << "Deleted " << totalDeleted << " old records from ping_results table" << std::endl;
    
    // 清理hosts表中长时间未见的数据（超过2倍保留天数）
    std::ostringstream cleanupHostsSQLStream;
    cleanupHostsSQLStream << "DELETE FROM hosts WHERE last_seen < NOW() - INTERVAL '" << (days * 2) << " days';";
    
    PGresult* cleanupHostsRes = PQexec(conn, cleanupHostsSQLStream.str().c_str());
    if (PQresultStatus(cleanupHostsRes) != PGRES_COMMAND_OK) {
        std::cerr << "Failed to cleanup old hosts: " << PQresultErrorMessage(cleanupHostsRes) << std::endl;
        PQclear(cleanupHostsRes);
        return;
    }
    
    int hostsDeleted = atoi(PQcmdTuples(cleanupHostsRes));
    PQclear(cleanupHostsRes);
    
    if (hostsDeleted > 0) {
        std::cout << "Deleted " << hostsDeleted << " old host records" << std::endl;
    }
    
    std::cout << "Cleanup completed." << std::endl;
}

std::map<std::string, std::string> DatabaseManagerPG::getAllHosts() {
    std::map<std::string, std::string> hosts;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return hosts;
    }
    
    // 查询所有主机，按IP排序以提高查询效率
    PGresult* res = executeQueryWithResult("SELECT ip, hostname FROM hosts ORDER BY ip;");
    if (!res) {
        std::cerr << "Failed to query hosts" << std::endl;
        return hosts;
    }
    
    for (int row = 0; row < PQntuples(res); row++) {
        char* ip = PQgetvalue(res, row, 0);
        char* hostname = PQgetvalue(res, row, 1);
        
        if (ip) {
            std::string ipStr = ip;
            std::string hostnameStr = hostname ? hostname : "";
            hosts[ipStr] = hostnameStr;
        }
    }
    
    PQclear(res);
    return hosts;
}

bool DatabaseManagerPG::addAlert(const std::string& ip, const std::string& hostname) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::cerr << "Invalid IP address format: " << ip << std::endl;
        return false;
    }
    
    // 插入或更新告警记录
    std::ostringstream alertSQLStream;
    alertSQLStream << "INSERT INTO alerts (ip, hostname, created_time) VALUES (" 
                   << escapeString(ip) << ", " << escapeString(hostname) << ", NOW() AT TIME ZONE 'UTC')"
                   << " ON CONFLICT (ip) DO NOTHING;";
    
    return executeQuery(alertSQLStream.str());
}

bool DatabaseManagerPG::removeAlert(const std::string& ip) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::cerr << "Invalid IP address format: " << ip << std::endl;
        return false;
    }
    
    // 获取告警信息，用于写入恢复记录
    std::ostringstream selectAlertSQLStream;
    selectAlertSQLStream << "SELECT hostname, created_time FROM alerts WHERE ip = " << escapeString(ip) << ";";
    
    PGresult* selectRes = executeQueryWithResult(selectAlertSQLStream.str());
    if (!selectRes) {
        std::cerr << "Failed to query alert information for IP: " << ip << std::endl;
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
    
    // 从告警表中删除记录
    std::ostringstream alertSQLStream;
    alertSQLStream << "DELETE FROM alerts WHERE ip = " << escapeString(ip) << ";";
    
    if (!executeQuery(alertSQLStream.str())) {
        return false;
    }
    
    // 如果找到了告警记录，将其写入恢复记录表
    if (!hostname.empty() && !alertTime.empty()) {
        std::ostringstream insertRecoverySQLStream;
        insertRecoverySQLStream << "INSERT INTO recovery_records (ip, hostname, alert_time, recovery_time) VALUES ("
                                << escapeString(ip) << ", " << escapeString(hostname) << ", " 
                                << escapeString(alertTime) << ", NOW() AT TIME ZONE 'UTC');";
        
        if (!executeQuery(insertRecoverySQLStream.str())) {
            std::cerr << "Failed to insert recovery record for IP: " << ip << std::endl;
            return false;
        }
    }
    
    return true;
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManagerPG::getActiveAlerts(int days) {
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return alerts;
    }
    
    // 查询活动告警，按创建时间排序
    std::string selectAlertsSQL;
    if (days >= 0) {
        // 查询指定天数内的告警
        std::ostringstream sqlStream;
        sqlStream << "SELECT ip, hostname, created_time FROM alerts WHERE created_time >= NOW() - INTERVAL '" << days << " days' ORDER BY created_time DESC;";
        selectAlertsSQL = sqlStream.str();
    } else {
        // 查询所有告警
        selectAlertsSQL = "SELECT ip, hostname, created_time FROM alerts ORDER BY created_time DESC;";
    }
    
    PGresult* res = executeQueryWithResult(selectAlertsSQL);
    if (!res) {
        std::cerr << "Failed to query alerts" << std::endl;
        return alerts;
    }
    
    // 预分配空间以提高性能
    alerts.reserve(PQntuples(res));
    
    for (int row = 0; row < PQntuples(res); row++) {
        char* ip = PQgetvalue(res, row, 0);
        char* hostname = PQgetvalue(res, row, 1);
        char* created_time = PQgetvalue(res, row, 2);
        
        if (ip) {
            std::string ipStr = ip ? ip : "";
            std::string hostnameStr = hostname ? hostname : "";
            std::string timeStr = created_time ? created_time : "";
            alerts.emplace_back(ipStr, hostnameStr, timeStr);
        }
    }
    
    PQclear(res);
    return alerts;
}
std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> DatabaseManagerPG::getRecoveryRecords(int days) {
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> records;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return records;
    }
    
    // 查询恢复记录，按恢复时间排序
    std::string selectRecordsSQL;
    if (days >= 0) {
        // 查询指定天数内的恢复记录
        std::ostringstream sqlStream;
        sqlStream << "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records WHERE recovery_time >= NOW() - INTERVAL '" << days << " days' ORDER BY recovery_time DESC;";
        selectRecordsSQL = sqlStream.str();
    } else {
        // 查询所有恢复记录
        selectRecordsSQL = "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records ORDER BY recovery_time DESC;";
    }
    
    PGresult* res = executeQueryWithResult(selectRecordsSQL);
    if (!res) {
        std::cerr << "Failed to query recovery records" << std::endl;
        return records;
    }
    
    // 预分配空间以提高性能
    records.reserve(PQntuples(res));
    
    for (int row = 0; row < PQntuples(res); row++) {
        int id = atoi(PQgetvalue(res, row, 0));
        char* ip = PQgetvalue(res, row, 1);
        char* hostname = PQgetvalue(res, row, 2);
        char* alert_time = PQgetvalue(res, row, 3);
        char* recovery_time = PQgetvalue(res, row, 4);
        
        std::string ipStr = ip ? ip : "";
        std::string hostnameStr = hostname ? hostname : "";
        std::string alertTimeStr = alert_time ? alert_time : "";
        std::string recoveryTimeStr = recovery_time ? recovery_time : "";
        
        records.emplace_back(id, ipStr, hostnameStr, alertTimeStr, recoveryTimeStr);
    }
    
    PQclear(res);
    return records;
}