#include "database_manager_pg.h"
#include <iostream>
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
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Query failed: " << PQresultErrorMessage(res) << std::endl;
        PQclear(res);
        return false;
    }
    
    PQclear(res);
    return true;
}

PGresult* DatabaseManagerPG::executeQueryWithResult(const std::string& query) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return nullptr;
    }
    
    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Query failed: " << PQresultErrorMessage(res) << std::endl;
        PQclear(res);
        return nullptr;
    }
    
    return res;
}

bool DatabaseManagerPG::initialize() {
    conn = PQconnectdb(connInfo.c_str());
    
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "Failed to connect to database: " << PQerrorMessage(conn) << std::endl;
        return false;
    }
    
    // 设置client_min_messages参数以抑制NOTICE消息
    // 检查连接字符串中是否包含client_min_messages参数
    if (connInfo.find("client_min_messages") == std::string::npos) {
        PGresult* res = PQexec(conn, "SET client_min_messages TO WARNING;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "Failed to set client_min_messages: " << PQresultErrorMessage(res) << std::endl;
            PQclear(res);
            return false;
        }
        PQclear(res);
    }
    
    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip INET PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            last_seen TIMESTAMP
        );
    )";
    
    if (!executeQuery(createHostsTableSQL)) {
        std::cerr << "Failed to create hosts table" << std::endl;
        return false;
    }
    
    // 创建alerts表，用于存储告警信息
    const char* createAlertsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS alerts (
            ip INET PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMP
        );
    )";
    
    if (!executeQuery(createAlertsTableSQL)) {
        std::cerr << "Failed to create alerts table" << std::endl;
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
    for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
        if (!isValidIP(ip)) {
            std::cerr << "Invalid IP address format: " << ip << std::endl;
            success = false;
            break;
        }
    }
    
    if (success) {
        // 在hosts表中批量插入或更新IP与主机名的映射关系
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
        
        if (!executeQuery(hostSQLStream.str())) {
            std::cerr << "Failed to insert host information" << std::endl;
            success = false;
        }
    }
    
    if (success) {
        // 为每个IP地址创建表（如果尚未创建）并插入ping结果
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
                success = false;
                break;
            }
            
            // 为timestamp列创建索引以提高查询性能
            std::ostringstream createIndexSQLStream;
            createIndexSQLStream << "CREATE INDEX IF NOT EXISTS idx_ping_" << std::regex_replace(ip, std::regex(R"(\.)"), "_") << "_timestamp "
                                 << "ON ping_" << std::regex_replace(ip, std::regex(R"(\.)"), "_") << " (timestamp);";
            
            if (!executeQuery(createIndexSQLStream.str())) {
                std::cerr << "Failed to create index for IP " << ip << std::endl;
                success = false;
                break;
            }
            
            // 插入ping结果
            std::ostringstream insertSQLStream;
            insertSQLStream << "INSERT INTO ping_" << std::regex_replace(ip, std::regex(R"(\.)"), "_") 
                            << " (delay, success, timestamp) VALUES ("
                            << delay << ", " << (successFlag ? "true" : "false") << ", " << escapeString(timestamp) << ");";
            
            if (!executeQuery(insertSQLStream.str())) {
                std::cerr << "Failed to insert ping result for IP " << ip << std::endl;
                success = false;
                break;
            }
        }
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

void DatabaseManagerPG::queryConsecutiveFailures(int failureCount) {
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return;
    }
    
    // 获取所有IP地址
    PGresult* hostsRes = executeQueryWithResult("SELECT ip, hostname FROM hosts;");
    if (!hostsRes) {
        std::cerr << "Failed to query hosts" << std::endl;
        return;
    }
    
    std::cout << "Hosts with " << failureCount << " consecutive failures:" << std::endl;
    std::cout << "=========================================================" << std::endl;
    
    for (int row = 0; row < PQntuples(hostsRes); row++) {
        char* ip = PQgetvalue(hostsRes, row, 0);
        char* hostname = PQgetvalue(hostsRes, row, 1);
        
        if (ip) {
            std::string ipStr = ip;
            std::string hostnameStr = hostname ? hostname : "";
            std::string tableName = "ping_" + std::regex_replace(ipStr, std::regex(R"(\.)"), "_");
            
            // 查询该IP的ping记录，按时间排序
            std::ostringstream querySQLStream;
            querySQLStream << "SELECT success, timestamp FROM " << tableName << " ORDER BY timestamp;";
            
            PGresult* queryRes = executeQueryWithResult(querySQLStream.str());
            if (!queryRes) {
                std::cerr << "Failed to query ping records for IP " << ipStr << std::endl;
                continue;
            }
            
            // 检查是否存在连续失败的记录
            int consecutiveFailureCount = 0;
            std::vector<std::string> currentFailureTimestamps;  // 记录当前连续失败的时间戳
            std::vector<std::string> lastMatchingFailures;     // 记录最后匹配的连续失败时间戳
            
            for (int i = 0; i < PQntuples(queryRes); i++) {
                char* success = PQgetvalue(queryRes, i, 0);
                char* timestamp = PQgetvalue(queryRes, i, 1);
                
                if (success && strcmp(success, "f") == 0) {
                    // 失败记录，增加连续失败计数
                    consecutiveFailureCount++;
                    if (timestamp) {
                        currentFailureTimestamps.push_back(std::string(timestamp));
                    }
                    
                    // 如果连续失败次数达到要求，保存这组连续失败的时间戳
                    if (consecutiveFailureCount >= failureCount) {
                        // 保存最后failureCount个连续失败时间戳
                        int count = currentFailureTimestamps.size();
                        lastMatchingFailures.clear();
                        int start = std::max(0, count - failureCount);
                        for (int j = start; j < count; j++) {
                            lastMatchingFailures.push_back(currentFailureTimestamps[j]);
                        }
                    }
                } else {
                    // 成功记录，重置连续失败计数
                    consecutiveFailureCount = 0;
                    currentFailureTimestamps.clear();
                }
            }
            
            PQclear(queryRes);
            
            // 如果存在满足条件的连续失败，则输出信息
            if (!lastMatchingFailures.empty()) {
                std::string hostInfo = ipStr + " (" + hostnameStr + ")";
                std::cout << hostInfo << ":" << std::endl;
                
                // 输出最后满足条件的连续失败时间戳
                for (const auto& ts : lastMatchingFailures) {
                    std::cout << "  " << ts << std::endl;
                }
                std::cout << std::endl;
            }
        }
    }
    
    PQclear(hostsRes);
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
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;
    
    if (!conn) {
        std::cerr << "Database not initialized" << std::endl;
        return alerts;
    }
    
    // 查询所有活动告警
    PGresult* res = executeQueryWithResult("SELECT ip, hostname, created_time FROM alerts;");
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