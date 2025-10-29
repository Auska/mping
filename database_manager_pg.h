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
    PGconn* conn;
    std::string connInfo;

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
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts();  // 返回IP, hostname, created_time
    
private:
    std::string escapeString(const std::string& str);
    bool isValidIP(const std::string& ip);
    bool executeQuery(const std::string& query);
    PGresult* executeQueryWithResult(const std::string& query);
};

#endif // DATABASE_MANAGER_PG_H