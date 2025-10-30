#ifndef DATABASE_MANAGER_PG_H
#define DATABASE_MANAGER_PG_H

#include <string>
#include <vector>
#include <tuple>
#include <map>
#include <libpq-fe.h>
#include <regex>

class DatabaseManagerPG {
private:
    std::string connInfo;
    PGconn* conn;

public:
    DatabaseManagerPG(const std::string& connectionInfo);
    ~DatabaseManagerPG();
    
    bool initialize();
    bool insertPingResult(const std::string& ip, const std::string& hostname, short delay, bool success, const std::string& timestamp);
    bool insertPingResults(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    void queryIPStatistics(const std::string& ip);
    void cleanupOldData(int days = 30);
    std::map<std::string, std::string> getAllHosts();
    
    // 告警表相关方法
    bool addAlert(const std::string& ip, const std::string& hostname);
    bool removeAlert(const std::string& ip);
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(int days = -1);  // 返回指定天数内的告警，-1表示获取所有告警
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts();  // 兼容旧接口
    
    // 恢复记录相关方法
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> getRecoveryRecords(int days = -1);  // 返回指定天数内的恢复记录，-1表示获取所有恢复记录
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> getRecoveryRecords();  // 兼容旧接口
    
private:
    // 辅助方法
    bool validateIPs(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    bool createIPTables(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    bool insertHostsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    bool insertPingResultsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    bool isValidIP(const std::string& ip);
    std::string escapeString(const std::string& str);
    bool executeQuery(const std::string& query);
    PGresult* executeQueryWithResult(const std::string& query);
};

#endif // DATABASE_MANAGER_PG_H