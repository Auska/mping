#include "db.h"
#include <iostream>
#include <sqlite3.h>
#include <sstream>
#include <algorithm>
#include <cctype>

Database::Database(const std::string& path) : dbPath(path), db(nullptr) {}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

// 辅助函数：将IP地址转换为有效的表名
std::string ipToTableName(const std::string& ip) {
    std::string tableName = "ip_" + ip;
    // 将点号替换为下划线，确保表名有效
    std::replace(tableName.begin(), tableName.end(), '.', '_');
    return tableName;
}

bool Database::initialize() {
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip TEXT PRIMARY KEY,
            hostname TEXT
        );
    )";
    
    char* errMsg = 0;
    rc = sqlite3_exec(db, createHostsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error creating hosts table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

// 为特定IP地址创建表
bool Database::createIPTable(const std::string& ip) {
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
    
    return true;
}

bool Database::insertPingResult(const std::string& ip, const std::string& hostname, long long delay, bool success, const std::string& timestamp) {
    if (!db) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    // 首先确保为该IP地址创建了表
    if (!createIPTable(ip)) {
        return false;
    }
    
    // 在hosts表中插入或更新IP与主机名的映射关系
    const char* upsertHostSQL = R"(
        INSERT OR REPLACE INTO hosts (ip, hostname)
        VALUES (?, ?);
    )";
    
    sqlite3_stmt* hostStmt;
    int rc = sqlite3_prepare_v2(db, upsertHostSQL, -1, &hostStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare host statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // 绑定参数
    sqlite3_bind_text(hostStmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(hostStmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
    
    // 执行插入/更新
    rc = sqlite3_step(hostStmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute host statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(hostStmt);
        return false;
    }
    
    sqlite3_finalize(hostStmt);
    
    // 在特定IP的表中插入ping结果
    std::string tableName = ipToTableName(ip);
    std::ostringstream insertSQLStream;
    insertSQLStream << "INSERT INTO " << tableName << " (delay, success, timestamp)"
                    << "VALUES (?, ?, ?);";
    
    std::string insertSQL = insertSQLStream.str();
    
    sqlite3_stmt* pingStmt;
    rc = sqlite3_prepare_v2(db, insertSQL.c_str(), -1, &pingStmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare ping statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // 绑定参数
    sqlite3_bind_int64(pingStmt, 1, delay);
    sqlite3_bind_int(pingStmt, 2, success ? 1 : 0);
    sqlite3_bind_text(pingStmt, 3, timestamp.c_str(), -1, SQLITE_STATIC);
    
    // 执行插入
    rc = sqlite3_step(pingStmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute ping statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(pingStmt);
        return false;
    }
    
    sqlite3_finalize(pingStmt);
    return true;
}