#include "database_manager_pg.h"
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
    
    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return nullptr;
    }
    return res;
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
    
    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            last_seen TIMESTAMP
        );
    )";
    
    if (!executeQuery(createHostsTableSQL)) {
        std::println(std::cerr, "Failed to create hosts table");
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

// 辅助函数：创建IP表和索引
bool DatabaseManagerPG::createIPTables(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        // 创建特定IP的表
        std::ostringstream createTableSQLStream;
        createTableSQLStream << "CREATE TABLE IF NOT EXISTS ping_" << std::regex_replace(ip, std::regex(R"(\.)"), "_") << " ("
                             << "id SERIAL PRIMARY KEY,"
                             << "delay INTEGER,"
                             << "success BOOLEAN,"
                             << "timestamp TIMESTAMP"
                             << ");";
        
        if (!executeQuery(createTableSQLStream.str())) {
            std::cerr << "Failed to create table for IP " << ip << std::endl;
            return false;
        }
        
        // 为timestamp列创建索引以提高查询性能
        std::ostringstream createIndexSQLStream;
        createIndexSQLStream << "CREATE INDEX IF NOT EXISTS idx_ping_" << std::regex_replace(ip, std::regex(R"(\.)"), "_") << "_timestamp "
                             << "ON ping_" << std::regex_replace(ip, std::regex(R"(\.)"), "_") << " (timestamp);";
        
        if (!executeQuery(createIndexSQLStream.str())) {
            std::cerr << "Failed to create index for IP " << ip << std::endl;
            return false;
        }
    }
    return true;
}

// 辅助函数：批量插入主机信息
bool DatabaseManagerPG::insertHostsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    std::ostringstream hostSQLStream;
    hostSQLStream << "INSERT INTO hosts (ip, hostname, last_seen) VALUES ";
    
    bool first = true;
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        if (!first) hostSQLStream << ", ";
        hostSQLStream << "(" << escapeString(ip) << ", " << escapeString(hostname) << ", NOW())";
        first = false;
    }
    
    hostSQLStream << " ON CONFLICT (ip) DO UPDATE SET "
                  << "hostname = EXCLUDED.hostname, "
                  << "last_seen = EXCLUDED.last_seen;";
    
    return executeQuery(hostSQLStream.str());
}

// 辅助函数：批量插入ping结果
bool DatabaseManagerPG::insertPingResultsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    // 构建批量插入语句
    std::map<std::string, std::vector<std::string>> batchInserts;
    
    // 将结果按IP分组
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        std::string tableName = "ping_" + std::regex_replace(ip, std::regex(R"(\.)"), "_");
        std::ostringstream insertSQLStream;
        insertSQLStream << "(" << delay << ", " << (successFlag ? "true" : "false") << ", " << escapeString(timestamp) << ")";
        batchInserts[tableName].push_back(insertSQLStream.str());
    }
    
    // 为每个表执行批量插入
    for (const auto& [tableName, values] : batchInserts) {
        std::ostringstream batchInsertSQLStream;
        batchInsertSQLStream << "INSERT INTO " << tableName << " (delay, success, timestamp) VALUES ";
        
        bool first = true;
        for (const auto& value : values) {
            if (!first) batchInsertSQLStream << ", ";
            batchInsertSQLStream << value;
            first = false;
        }
        batchInsertSQLStream << ";";
        
        if (!executeQuery(batchInsertSQLStream.str())) {
            std::cerr << "Failed to insert ping results for table " << tableName << std::endl;
            return false;
        }
    }
    
    return true;
}

bool DatabaseManagerPG::insertPingResults(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    if (results.empty()) {
        return true; // 没有结果需要插入，视为成功
    }
    
    // 开始事务以提高性能
    if (!executeQuery("BEGIN;")) {
        std::cerr << "Failed to begin transaction" << std::endl;
        return false;
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
    
    // 提交或回滚事务
    if (success) {
        if (!executeQuery("COMMIT;")) {
            std::cerr << "Failed to commit transaction" << std::endl;
            success = false;
        }
    } else {
        executeQuery("ROLLBACK;");
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
    
    // 查询特定IP的表
    std::string tableName = "ping_" + std::regex_replace(ip, std::regex(R"(\.)"), "_");
    
    // 获取总记录数
    std::ostringstream countSQLStream;
    countSQLStream << "SELECT COUNT(*) FROM " << tableName << ";";
    
    PGresult* countRes = executeQueryWithResult(countSQLStream.str());
    if (!countRes) {
        std::cerr << "Failed to query count" << std::endl;
        return;
    }
    
    int totalRecords = 0;
    if (PQntuples(countRes) > 0) {
        totalRecords = atoi(PQgetvalue(countRes, 0, 0));
    }
    PQclear(countRes);
    
    std::cout << "Total ping records: " << totalRecords << std::endl;
    
    if (totalRecords == 0) {
        std::cout << "No ping records found for this IP." << std::endl;
        return;
    }
    
    // 获取成功和失败的次数
    std::ostringstream successSQLStream;
    successSQLStream << "SELECT COUNT(*) FROM " << tableName << " WHERE success = true;";
    
    PGresult* successRes = executeQueryWithResult(successSQLStream.str());
    if (!successRes) {
        std::cerr << "Failed to query success count" << std::endl;
        return;
    }
    
    int successCount = 0;
    if (PQntuples(successRes) > 0) {
        successCount = atoi(PQgetvalue(successRes, 0, 0));
    }
    PQclear(successRes);
    
    int failureCount = totalRecords - successCount;
    double successRate = (totalRecords > 0) ? (double)successCount / totalRecords * 100 : 0;
    double failureRate = (totalRecords > 0) ? (double)failureCount / totalRecords * 100 : 0;
    
    std::cout << "Successful pings: " << successCount << std::endl;
    std::cout << "Failed pings: " << failureCount << std::endl;
    std::cout << "Success rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
    std::cout << "Failure rate: " << std::fixed << std::setprecision(2) << failureRate << "%" << std::endl;
    
    // 获取平均延迟
    std::ostringstream avgDelaySQLStream;
    avgDelaySQLStream << "SELECT AVG(delay) FROM " << tableName << " WHERE success = true;";
    
    PGresult* avgDelayRes = executeQueryWithResult(avgDelaySQLStream.str());
    if (!avgDelayRes) {
        std::cerr << "Failed to query average delay" << std::endl;
        return;
    }
    
    double avgDelay = 0;
    if (PQntuples(avgDelayRes) > 0) {
        char* avgText = PQgetvalue(avgDelayRes, 0, 0);
        if (avgText) {
            avgDelay = atof(avgText);
        }
    }
    PQclear(avgDelayRes);
    
    std::cout << "Average delay (successful pings): " << std::fixed << std::setprecision(2) << avgDelay << "ms" << std::endl;
    
    // 获取最大和最小延迟
    std::ostringstream maxMinDelaySQLStream;
    maxMinDelaySQLStream << "SELECT MAX(delay), MIN(delay) FROM " << tableName << " WHERE success = true;";
    
    PGresult* maxMinDelayRes = executeQueryWithResult(maxMinDelaySQLStream.str());
    if (!maxMinDelayRes) {
        std::cerr << "Failed to query max/min delay" << std::endl;
        return;
    }
    
    int maxDelay = 0, minDelay = 0;
    if (PQntuples(maxMinDelayRes) > 0) {
        char* maxText = PQgetvalue(maxMinDelayRes, 0, 0);
        char* minText = PQgetvalue(maxMinDelayRes, 0, 1);
        if (maxText) maxDelay = atoi(maxText);
        if (minText) minDelay = atoi(minText);
    }
    PQclear(maxMinDelayRes);
    
    std::cout << "Maximum delay (successful pings): " << maxDelay << "ms" << std::endl;
    std::cout << "Minimum delay (successful pings): " << minDelay << "ms" << std::endl;
    
    // 显示最近的10条记录
    std::ostringstream recentSQLStream;
    recentSQLStream << "SELECT delay, success, timestamp FROM " << tableName << " ORDER BY timestamp DESC LIMIT 10;";
    
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
    
    // 获取所有IP地址
    PGresult* hostsRes = executeQueryWithResult("SELECT ip FROM hosts;");
    if (!hostsRes) {
        std::cerr << "Failed to query hosts" << std::endl;
        return;
    }
    
    int totalDeleted = 0;
    
    for (int row = 0; row < PQntuples(hostsRes); row++) {
        char* ip = PQgetvalue(hostsRes, row, 0);
        
        if (ip) {
            std::string ipStr = ip;
            std::string tableName = "ping_" + std::regex_replace(ipStr, std::regex(R"(\.)"), "_");
            
            // 删除指定天数之前的数据
            std::ostringstream deleteSQLStream;
            deleteSQLStream << "DELETE FROM " << tableName << " WHERE timestamp < NOW() - INTERVAL '" << days << " days';";
            
            PGresult* deleteRes = PQexec(conn, deleteSQLStream.str().c_str());
            if (PQresultStatus(deleteRes) != PGRES_COMMAND_OK) {
                std::cerr << "Failed to delete old data for IP " << ipStr << ": " << PQresultErrorMessage(deleteRes) << std::endl;
                PQclear(deleteRes);
                continue;
            }
            
            int deletedRows = atoi(PQcmdTuples(deleteRes));
            totalDeleted += deletedRows;
            PQclear(deleteRes);
            
            if (deletedRows > 0) {
                std::cout << "Deleted " << deletedRows << " old records for IP " << ipStr << std::endl;
            }
        }
    }
    
    PQclear(hostsRes);
    
    // 清理hosts表中没有关联数据的IP记录
    // 注意：这在PostgreSQL中需要特殊处理，因为表名不能直接在查询中参数化
    // 这里简化处理，仅输出提示信息
    std::cout << "Note: Cleaning unused host records is not implemented in this PostgreSQL version." << std::endl;
    
    std::cout << "Total deleted records: " << totalDeleted << std::endl;
    std::cout << "Cleanup completed." << std::endl;
}

std::map<std::string, std::string> DatabaseManagerPG::getAllHosts() {
    std::map<std::string, std::string> hosts;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return hosts;
    }
    
    // 查询所有主机
    PGresult* res = executeQueryWithResult("SELECT ip, hostname FROM hosts;");
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
                   << escapeString(ip) << ", " << escapeString(hostname) << ", NOW())"
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
    
    // 从告警表中删除记录
    std::ostringstream alertSQLStream;
    alertSQLStream << "DELETE FROM alerts WHERE ip = " << escapeString(ip) << ";";
    
    return executeQuery(alertSQLStream.str());
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManagerPG::getActiveAlerts() {
    return getActiveAlerts(-1);  // -1表示获取所有告警
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManagerPG::getActiveAlerts(int days) {
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return alerts;
    }
    
    // 查询活动告警
    std::string selectAlertsSQL;
    if (days >= 0) {
        // 查询指定天数内的告警
        std::ostringstream sqlStream;
        sqlStream << "SELECT ip, hostname, created_time FROM alerts WHERE created_time >= NOW() - INTERVAL '" << days << " days';";
        selectAlertsSQL = sqlStream.str();
    } else {
        // 查询所有告警
        selectAlertsSQL = "SELECT ip, hostname, created_time FROM alerts;";
    }
    
    PGresult* res = executeQueryWithResult(selectAlertsSQL);
    if (!res) {
        std::cerr << "Failed to query alerts" << std::endl;
        return alerts;
    }
    
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
std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> DatabaseManagerPG::getRecoveryRecords() {
    return getRecoveryRecords(-1);  // -1表示获取所有恢复记录
}

std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> DatabaseManagerPG::getRecoveryRecords(int days) {
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> records;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return records;
    }
    
    // 查询恢复记录
    std::string selectRecordsSQL;
    if (days >= 0) {
        // 查询指定天数内的恢复记录
        std::ostringstream sqlStream;
        sqlStream << "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records WHERE recovery_time >= NOW() - INTERVAL '" << days << " days';";
        selectRecordsSQL = sqlStream.str();
    } else {
        // 查询所有恢复记录
        selectRecordsSQL = "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records;";
    }
    
    PGresult* res = executeQueryWithResult(selectRecordsSQL);
    if (!res) {
        std::cerr << "Failed to query recovery records" << std::endl;
        return records;
    }
    
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