#include "database_manager_pg.h"
#include <iostream>
#include <vector>
#include <tuple>
#include <libpq-fe.h>
#include <sstream>

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