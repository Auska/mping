#include "db.h"
#include <iostream>
#include <sqlite3.h>

Database::Database(const std::string& path) : dbPath(path), db(nullptr) {}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

bool Database::initialize() {
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // 创建ping_results表
    const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS ping_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ip TEXT NOT NULL,
            hostname TEXT,
            delay INTEGER,
            success BOOLEAN,
            timestamp TEXT
        );
    )";
    
    char* errMsg = 0;
    rc = sqlite3_exec(db, createTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
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
    
    const char* insertSQL = R"(
        INSERT INTO ping_results (ip, hostname, delay, success, timestamp)
        VALUES (?, ?, ?, ?, ?);
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // 绑定参数
    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hostname.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, delay);
    sqlite3_bind_int(stmt, 4, success ? 1 : 0);
    sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_STATIC);
    
    // 执行插入
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    
    sqlite3_finalize(stmt);
    return true;
}