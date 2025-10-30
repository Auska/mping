#include "database_manager.h"
#include <iostream>
#include <sqlite3.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <regex>
#include <stdexcept>

DatabaseManager::DatabaseManager(const std::string& path) : dbPath(path), db(nullptr) {
    if (path.empty()) {
        throw std::invalid_argument("Database path cannot be empty");
    }
}

DatabaseManager::~DatabaseManager() {
    if (db) {
        sqlite3_close(db);
    }
}

// 辅助函数：将IP地址转换为有效的表名
std::string DatabaseManager::ipToTableName(const std::string& ip) {
    std::string tableName = "ip_" + ip;
    // 将点号替换为下划线，确保表名有效
    std::replace(tableName.begin(), tableName.end(), '.', '_');
    return tableName;
}

bool DatabaseManager::isValidIP(const std::string& ip) {
    // 使用正则表达式验证IPv4地址格式
    std::regex ipPattern("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    return std::regex_match(ip, ipPattern);
}

bool DatabaseManager::initialize() {
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::string errorMsg = "Can't open database: ";
        if (db) {
            errorMsg += sqlite3_errmsg(db);
        } else {
            errorMsg += "Unknown error";
        }
        std::cerr << errorMsg << std::endl;
        return false;
    }
    
    if (!db) {
        std::cerr << "Database handle is null after opening" << std::endl;
        return false;
    }
    
    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TEXT DEFAULT CURRENT_TIMESTAMP,
            last_seen TEXT
        );
    )";
    
    char* errMsg = 0;
    rc = sqlite3_exec(db, createHostsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::string errorMsg = "SQL error creating hosts table: ";
        if (errMsg) {
            errorMsg += errMsg;
            sqlite3_free(errMsg);
        } else {
            errorMsg += "Unknown error";
        }
        std::cerr << errorMsg << std::endl;
        return false;
    }
    
    // 创建alerts表，用于存储告警信息
    const char* createAlertsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS alerts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TEXT
        );
    )";
    
    rc = sqlite3_exec(db, createAlertsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::string errorMsg = "SQL error creating alerts table: ";
        if (errMsg) {
            errorMsg += errMsg;
            sqlite3_free(errMsg);
        } else {
            errorMsg += "Unknown error";
        }
        std::cerr << errorMsg << std::endl;
        return false;
    }
    
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
    
    rc = sqlite3_exec(db, createRecoveryTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::string errorMsg = "SQL error creating recovery_records table: ";
        if (errMsg) {
            errorMsg += errMsg;
            sqlite3_free(errMsg);
        } else {
            errorMsg += "Unknown error";
        }
        std::cerr << errorMsg << std::endl;
        return false;
    }
    
    return true;
}

// 为特定IP地址创建表
bool DatabaseManager::createIPTable(const std::string& ip) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    std::string tableName = ipToTableName(ip);
    
    // 创建特定IP的表
    std::ostringstream createTableSQLStream;
    createTableSQLStream << "CREATE TABLE IF NOT EXISTS " << tableName << " ("
                         << "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         << "delay INTEGER,"
                         << "success BOOLEAN,"
                         << "timestamp TEXT"
                         << ");";
    
    std::string createTableSQL = createTableSQLStream.str();
    
    char* errMsg = 0;
    int rc = sqlite3_exec(db, createTableSQL.c_str(), 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error creating table for IP " << ip << ": " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    // 为timestamp列创建索引以提高查询性能
    std::ostringstream createIndexSQLStream;
    createIndexSQLStream << "CREATE INDEX IF NOT EXISTS idx_" << tableName << "_timestamp "
                         << "ON " << tableName << " (timestamp);";
    
    std::string createIndexSQL = createIndexSQLStream.str();
    
    rc = sqlite3_exec(db, createIndexSQL.c_str(), 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error creating index for IP " << ip << ": " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool DatabaseManager::insertPingResult(const std::string& ip, const std::string& hostname, short delay, bool success, const std::string& timestamp) {
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

bool DatabaseManager::insertPingResults(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    if (results.empty()) {
        return true; // 没有结果需要插入，视为成功
    }
    
    // 开始事务以提高性能
    char* errMsg = 0;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to begin transaction: " << errMsg << std::endl;
        sqlite3_free(errMsg);
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
        // 为所有IP地址创建表（如果尚未创建）
        for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
            if (!createIPTable(ip)) {
                success = false;
                break;
            }
        }
    }
    
    if (success) {
        // 在hosts表中批量插入或更新IP与主机名的映射关系
        const char* upsertHostSQL = R"(
            INSERT INTO hosts (ip, hostname, last_seen)
            VALUES (?, ?, datetime('now'))
            ON CONFLICT(ip) DO UPDATE SET
            hostname = excluded.hostname,
            last_seen = excluded.last_seen;
        )";
        
        sqlite3_stmt* hostStmt;
        rc = sqlite3_prepare_v2(db, upsertHostSQL, -1, &hostStmt, 0);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare host statement: " << sqlite3_errmsg(db) << std::endl;
            success = false;
        } else {
            // 为每个结果执行主机信息插入/更新
            for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
                sqlite3_bind_text(hostStmt, 1, ip.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(hostStmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
                
                rc = sqlite3_step(hostStmt);
                if (rc != SQLITE_DONE) {
                    std::cerr << "Failed to execute host statement: " << sqlite3_errmsg(db) << std::endl;
                    success = false;
                    break;
                }
                
                // 重置语句以供下一次使用
                sqlite3_reset(hostStmt);
            }
            sqlite3_finalize(hostStmt);
        }
    }
    
    if (success) {
        // 为每个IP地址批量插入ping结果
        std::map<std::string, sqlite3_stmt*> pingStmts;
        
        for (const auto& [ip, hostname, delay, successFlag, timestamp] : results) {
            std::string tableName = ipToTableName(ip);
            
            // 如果还没有为这个IP创建语句，创建一个
            if (pingStmts.find(ip) == pingStmts.end()) {
                std::ostringstream insertSQLStream;
                insertSQLStream << "INSERT INTO " << tableName << " (delay, success, timestamp)"
                                << "VALUES (?, ?, ?);";
                
                std::string insertSQL = insertSQLStream.str();
                
                sqlite3_stmt* pingStmt;
                rc = sqlite3_prepare_v2(db, insertSQL.c_str(), -1, &pingStmt, 0);
                if (rc != SQLITE_OK) {
                    std::cerr << "Failed to prepare ping statement for IP " << ip << ": " << sqlite3_errmsg(db) << std::endl;
                    success = false;
                    break;
                }
                
                pingStmts[ip] = pingStmt;
            }
            
            // 绑定参数并执行插入
            sqlite3_stmt* pingStmt = pingStmts[ip];
            sqlite3_bind_int64(pingStmt, 1, delay);
            sqlite3_bind_int(pingStmt, 2, successFlag ? 1 : 0);
            sqlite3_bind_text(pingStmt, 3, timestamp.c_str(), -1, SQLITE_STATIC);
            
            rc = sqlite3_step(pingStmt);
            if (rc != SQLITE_DONE) {
                std::cerr << "Failed to execute ping statement for IP " << ip << ": " << sqlite3_errmsg(db) << std::endl;
                success = false;
                break;
            }
            
            // 重置语句以供下一次使用
            sqlite3_reset(pingStmt);
        }
        
        // 释放所有语句
        for (auto& [ip, stmt] : pingStmts) {
            sqlite3_finalize(stmt);
        }
    }
    
    // 提交或回滚事务
    if (success) {
        rc = sqlite3_exec(db, "COMMIT;", 0, 0, &errMsg);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to commit transaction: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            success = false;
        }
    } else {
        rc = sqlite3_exec(db, "ROLLBACK;", 0, 0, &errMsg);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to rollback transaction: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    }
    
    return success;
}

void DatabaseManager::queryIPStatistics(const std::string& ip) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return;
    }
    
    // 获取主机名
    const char* selectHostSQL = "SELECT hostname FROM hosts WHERE ip = ?;";
    sqlite3_stmt* hostStmt;
    int rc = sqlite3_prepare_v2(db, selectHostSQL, -1, &hostStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare host query statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    sqlite3_bind_text(hostStmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    std::string hostname = "";
    if (sqlite3_step(hostStmt) == SQLITE_ROW) {
        const char* hostText = (const char*)sqlite3_column_text(hostStmt, 0);
        if (hostText) {
            hostname = hostText;
        }
    }
    sqlite3_finalize(hostStmt);
    
    std::cout << "Statistics for IP: " << ip << " (" << hostname << ")" << std::endl;
    std::cout << "=========================================================" << std::endl;
    
    // 查询特定IP的表
    std::string tableName = ipToTableName(ip);
    
    // 获取总记录数
    std::ostringstream countSQLStream;
    countSQLStream << "SELECT COUNT(*) FROM " << tableName << ";";
    std::string countSQL = countSQLStream.str();
    
    sqlite3_stmt* countStmt;
    rc = sqlite3_prepare_v2(db, countSQL.c_str(), -1, &countStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare count statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    int totalRecords = 0;
    if (sqlite3_step(countStmt) == SQLITE_ROW) {
        totalRecords = sqlite3_column_int(countStmt, 0);
    }
    sqlite3_finalize(countStmt);
    
    std::cout << "Total ping records: " << totalRecords << std::endl;
    
    if (totalRecords == 0) {
        std::cout << "No ping records found for this IP." << std::endl;
        return;
    }
    
    // 获取成功和失败的次数
    std::ostringstream successSQLStream;
    successSQLStream << "SELECT COUNT(*) FROM " << tableName << " WHERE success = 1;";
    std::string successSQL = successSQLStream.str();
    
    sqlite3_stmt* successStmt;
    rc = sqlite3_prepare_v2(db, successSQL.c_str(), -1, &successStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare success count statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    int successCount = 0;
    if (sqlite3_step(successStmt) == SQLITE_ROW) {
        successCount = sqlite3_column_int(successStmt, 0);
    }
    sqlite3_finalize(successStmt);
    
    int failureCount = totalRecords - successCount;
    double successRate = (totalRecords > 0) ? (double)successCount / totalRecords * 100 : 0;
    double failureRate = (totalRecords > 0) ? (double)failureCount / totalRecords * 100 : 0;
    
    std::cout << "Successful pings: " << successCount << std::endl;
    std::cout << "Failed pings: " << failureCount << std::endl;
    std::cout << "Success rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
    std::cout << "Failure rate: " << std::fixed << std::setprecision(2) << failureRate << "%" << std::endl;
    
    // 获取平均延迟
    std::ostringstream avgDelaySQLStream;
    avgDelaySQLStream << "SELECT AVG(delay) FROM " << tableName << " WHERE success = 1;";
    std::string avgDelaySQL = avgDelaySQLStream.str();
    
    sqlite3_stmt* avgDelayStmt;
    rc = sqlite3_prepare_v2(db, avgDelaySQL.c_str(), -1, &avgDelayStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare average delay statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    double avgDelay = 0;
    if (sqlite3_step(avgDelayStmt) == SQLITE_ROW) {
        avgDelay = sqlite3_column_double(avgDelayStmt, 0);
    }
    sqlite3_finalize(avgDelayStmt);
    
    std::cout << "Average delay (successful pings): " << std::fixed << std::setprecision(2) << avgDelay << "ms" << std::endl;
    
    // 获取最大和最小延迟
    std::ostringstream maxMinDelaySQLStream;
    maxMinDelaySQLStream << "SELECT MAX(delay), MIN(delay) FROM " << tableName << " WHERE success = 1;";
    std::string maxMinDelaySQL = maxMinDelaySQLStream.str();
    
    sqlite3_stmt* maxMinDelayStmt;
    rc = sqlite3_prepare_v2(db, maxMinDelaySQL.c_str(), -1, &maxMinDelayStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare max/min delay statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    int maxDelay = 0, minDelay = 0;
    if (sqlite3_step(maxMinDelayStmt) == SQLITE_ROW) {
        maxDelay = sqlite3_column_int(maxMinDelayStmt, 0);
        minDelay = sqlite3_column_int(maxMinDelayStmt, 1);
    }
    sqlite3_finalize(maxMinDelayStmt);
    
    std::cout << "Maximum delay (successful pings): " << maxDelay << "ms" << std::endl;
    std::cout << "Minimum delay (successful pings): " << minDelay << "ms" << std::endl;
    
    // 显示最近的10条记录
    std::ostringstream recentSQLStream;
    recentSQLStream << "SELECT delay, success, timestamp FROM " << tableName << " ORDER BY timestamp DESC LIMIT 10;";
    std::string recentSQL = recentSQLStream.str();
    
    sqlite3_stmt* recentStmt;
    rc = sqlite3_prepare_v2(db, recentSQL.c_str(), -1, &recentStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare recent records statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    std::cout << "\nRecent ping records (last 10):" << std::endl;
    std::cout << "Timestamp           \tDelay\tStatus" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    
    while (sqlite3_step(recentStmt) == SQLITE_ROW) {
        const char* timestamp = (const char*)sqlite3_column_text(recentStmt, 2);
        int delay = sqlite3_column_int(recentStmt, 0);
        int success = sqlite3_column_int(recentStmt, 1);
        
        std::cout << (timestamp ? timestamp : "N/A") << "\t" 
                  << delay << "ms\t" 
                  << (success ? "Success" : "Failed") << std::endl;
    }
    sqlite3_finalize(recentStmt);
}

void DatabaseManager::cleanupOldData(int days) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return;
    }
    
    std::cout << "Cleaning up data older than " << days << " days..." << std::endl;
    
    // 获取所有IP地址
    const char* selectHostsSQL = "SELECT ip FROM hosts;";
    sqlite3_stmt* hostsStmt;
    int rc = sqlite3_prepare_v2(db, selectHostsSQL, -1, &hostsStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare hosts query statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    
    int totalDeleted = 0;
    
    while (sqlite3_step(hostsStmt) == SQLITE_ROW) {
        const char* ip = (const char*)sqlite3_column_text(hostsStmt, 0);
        
        if (ip) {
            std::string ipStr = ip;
            std::string tableName = ipToTableName(ipStr);
            
            // 删除指定天数之前的数据
            std::ostringstream deleteSQLStream;
            deleteSQLStream << "DELETE FROM " << tableName << " WHERE timestamp < datetime('now', '-" << days << " days');";
            std::string deleteSQL = deleteSQLStream.str();
            
            char* errMsg = 0;
            rc = sqlite3_exec(db, deleteSQL.c_str(), 0, 0, &errMsg);
            if (rc != SQLITE_OK) {
                std::cerr << "SQL error deleting old data for IP " << ipStr << ": " << errMsg << std::endl;
                sqlite3_free(errMsg);
                continue;
            }
            
            int deletedRows = sqlite3_changes(db);
            totalDeleted += deletedRows;
            
            if (deletedRows > 0) {
                std::cout << "Deleted " << deletedRows << " old records for IP " << ipStr << std::endl;
            }
        }
    }
    
    sqlite3_finalize(hostsStmt);
    
    // 清理hosts表中没有关联数据的IP记录
    const char* cleanHostsSQL = R"(
        DELETE FROM hosts WHERE ip NOT IN (
            SELECT DISTINCT substr(name, 4) FROM sqlite_master 
            WHERE type = 'table' AND name LIKE 'ip_%'
        );
    )";
    
    char* errMsg = 0;
    rc = sqlite3_exec(db, cleanHostsSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error cleaning hosts table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } else {
        int deletedHosts = sqlite3_changes(db);
        if (deletedHosts > 0) {
            std::cout << "Deleted " << deletedHosts << " unused host records" << std::endl;
        }
    }
    
    std::cout << "Total deleted records: " << totalDeleted << std::endl;
    std::cout << "Cleanup completed." << std::endl;
}

std::map<std::string, std::string> DatabaseManager::getAllHosts() {
    std::map<std::string, std::string> hosts;
    
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return hosts;
    }
    
    // 查询所有主机
    const char* selectHostsSQL = "SELECT ip, hostname FROM hosts;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, selectHostsSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare hosts query statement: " << sqlite3_errmsg(db) << std::endl;
        return hosts;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* ip = (const char*)sqlite3_column_text(stmt, 0);
        const char* hostname = (const char*)sqlite3_column_text(stmt, 1);
        
        if (ip) {
            std::string ipStr = ip;
            std::string hostnameStr = hostname ? hostname : "";
            hosts[ipStr] = hostnameStr;
        }
    }
    
    sqlite3_finalize(stmt);
    return hosts;
}

bool DatabaseManager::addAlert(const std::string& ip, const std::string& hostname) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::cerr << "Invalid IP address format: " << ip << std::endl;
        return false;
    }
    
    // 插入或更新告警记录
    const char* insertAlertSQL = R"(
        INSERT INTO alerts (ip, hostname, created_time)
        VALUES (?, ?, datetime('now', 'localtime'))
        ON CONFLICT(ip) DO NOTHING;
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insertAlertSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare alert insert statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute alert insert statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    return true;
}

bool DatabaseManager::removeAlert(const std::string& ip) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    // 验证IP地址格式
    if (!isValidIP(ip)) {
        std::cerr << "Invalid IP address format: " << ip << std::endl;
        return false;
    }
    
    // 从告警表中删除记录
    const char* deleteAlertSQL = "DELETE FROM alerts WHERE ip = ?;";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, deleteAlertSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare alert delete statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute alert delete statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    return true;
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManager::getActiveAlerts(int days) {
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;
    
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return alerts;
    }
    
    // 查询活动告警
    std::string selectAlertsSQL;
    if (days >= 0) {
        // 查询指定天数内的告警
        std::ostringstream sqlStream;
        sqlStream << "SELECT ip, hostname, created_time FROM alerts WHERE created_time >= datetime('now', '-" << days << " days');";
        selectAlertsSQL = sqlStream.str();
    } else {
        // 查询所有告警
        selectAlertsSQL = "SELECT ip, hostname, created_time FROM alerts;";
    }
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, selectAlertsSQL.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare alerts query statement: " << sqlite3_errmsg(db) << std::endl;
        return alerts;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* ip = (const char*)sqlite3_column_text(stmt, 0);
        const char* hostname = (const char*)sqlite3_column_text(stmt, 1);
        const char* created_time = (const char*)sqlite3_column_text(stmt, 2);
        
        if (ip) {
            std::string ipStr = ip ? ip : "";
            std::string hostnameStr = hostname ? hostname : "";
            std::string timeStr = created_time ? created_time : "";
            alerts.emplace_back(ipStr, hostnameStr, timeStr);
        }
    }
    
    sqlite3_finalize(stmt);
    return alerts;
}
std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> DatabaseManager::getRecoveryRecords(int days) {
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> records;
    
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return records;
    }
    
    // 查询恢复记录
    std::string selectRecordsSQL;
    if (days >= 0) {
        // 查询指定天数内的恢复记录
        std::ostringstream sqlStream;
        sqlStream << "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records WHERE recovery_time >= datetime('now', '-" << days << " days');";
        selectRecordsSQL = sqlStream.str();
    } else {
        // 查询所有恢复记录
        selectRecordsSQL = "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records;";
    }
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, selectRecordsSQL.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare recovery records query statement: " << sqlite3_errmsg(db) << std::endl;
        return records;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* ip = (const char*)sqlite3_column_text(stmt, 1);
        const char* hostname = (const char*)sqlite3_column_text(stmt, 2);
        const char* alert_time = (const char*)sqlite3_column_text(stmt, 3);
        const char* recovery_time = (const char*)sqlite3_column_text(stmt, 4);
        
        std::string ipStr = ip ? ip : "";
        std::string hostnameStr = hostname ? hostname : "";
        std::string alertTimeStr = alert_time ? alert_time : "";
        std::string recoveryTimeStr = recovery_time ? recovery_time : "";
        
        records.emplace_back(id, ipStr, hostnameStr, alertTimeStr, recoveryTimeStr);
    }
    
    sqlite3_finalize(stmt);
    return records;
}