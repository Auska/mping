#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <string>
#include <sqlite3.h>
#include <vector>
#include <tuple>
#include <map>

class DatabaseManager {
private:
    sqlite3* db;
    std::string dbPath;

public:
    DatabaseManager(const std::string& path);
    ~DatabaseManager();
    
    bool initialize();
    bool insertPingResult(const std::string& ip, const std::string& hostname, short delay, bool success, const std::string& timestamp);
    void queryIPStatistics(const std::string& ip);
    void queryConsecutiveFailures(int failureCount);
    void cleanupOldData(int days = 30);
    std::map<std::string, std::string> getAllHosts();
    
private:
    bool createIPTable(const std::string& ip);
    std::string ipToTableName(const std::string& ip);
};

#endif // DATABASE_MANAGER_H