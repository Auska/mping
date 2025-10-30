#include "database_manager.h"
#include <iostream>
#include <vector>
#include <tuple>
#include <sqlite3.h>
#include <sstream>

std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> DatabaseManager::getRecoveryRecords() {
    return getRecoveryRecords(-1);  // -1表示获取所有恢复记录
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