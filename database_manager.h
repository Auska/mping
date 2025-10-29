#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <string>
#include <sqlite3.h>
#include <vector>
#include <tuple>
#include <map>
#include <regex>

class DatabaseManager {
private:
    sqlite3* db;
    std::string dbPath;

public:
    DatabaseManager(const std::string& path);
    ~DatabaseManager();
    
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
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(int days);  // 返回指定天数内的告警
    
private:
    bool createIPTable(const std::string& ip);
    std::string ipToTableName(const std::string& ip);
    bool isValidIP(const std::string& ip);
};

#endif // DATABASE_MANAGER_H