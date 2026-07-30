#ifndef DATABASE_BASE_H
#define DATABASE_BASE_H

#include <mutex>
#include <print>
#include <string>

#include "ip_validator.h"

struct PingStatistics {
    int totalRecords   = 0;
    int successCount   = 0;
    int failureCount   = 0;
    int maxDelay       = 0;
    int minDelay       = 0;
    double avgDelay    = 0;
    double successRate = 0;
    double failureRate = 0;
    std::string hostname;
};

class DatabaseBase {
   protected:
    std::mutex dbMutex;

    bool isValidIP(const std::string& ip) const { return IPValidator::isValidIPv4(ip); }

    DatabaseBase()          = default;
    virtual ~DatabaseBase() = default;

    DatabaseBase(const DatabaseBase&)            = delete;
    DatabaseBase& operator=(const DatabaseBase&) = delete;
    DatabaseBase(DatabaseBase&&)                 = delete;
    DatabaseBase& operator=(DatabaseBase&&)      = delete;

    friend class DatabaseBaseTest;
};

class DatabaseBaseTest : public DatabaseBase {
   public:
    static bool isValidIP(const std::string& ip) {
        DatabaseBaseTest instance;
        return instance.DatabaseBase::isValidIP(ip);
    }
};

#endif  // DATABASE_BASE_H
