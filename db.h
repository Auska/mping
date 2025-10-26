#ifndef DB_H
#define DB_H

#include <string>
#include <sqlite3.h>
#include <vector>
#include <tuple>

class Database {
private:
    sqlite3* db;
    std::string dbPath;

public:
    Database(const std::string& path);
    ~Database();
    
    bool initialize();
    bool insertPingResult(const std::string& ip, const std::string& hostname, long long delay, bool success, const std::string& timestamp);
    void queryIPStatistics(const std::string& ip);
    
private:
    bool createIPTable(const std::string& ip);
    std::string ipToTableName(const std::string& ip);
};

#endif // DB_H