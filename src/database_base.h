#ifndef DATABASE_BASE_H
#define DATABASE_BASE_H

#include <iostream>
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

    // 共享的统计输出方法
    void printStatisticsHeader(const std::string& ip, const std::string& hostname) {
        std::println(std::cout, "Statistics for IP: {} ({})", ip, hostname);
        std::println(std::cout, "=========================================================");
    }

    void printStatisticsBody(const PingStatistics& stats) {
        std::println(std::cout, "Total ping records: {}", stats.totalRecords);
        if (stats.totalRecords == 0) {
            std::println(std::cout, "No ping records found for this IP.");
            return;
        }
        std::println(std::cout, "Successful pings: {}", stats.successCount);
        std::println(std::cout, "Failed pings: {}", stats.failureCount);
        std::println(std::cout, "Success rate: {:.2f}%", stats.successRate);
        std::println(std::cout, "Failure rate: {:.2f}%", stats.failureRate);
        std::println(std::cout, "Average delay (successful pings): {:.2f}ms", stats.avgDelay);
        std::println(std::cout, "Maximum delay (successful pings): {}ms", stats.maxDelay);
        std::println(std::cout, "Minimum delay (successful pings): {}ms", stats.minDelay);
    }

    void printRecentRecordsHeader() {
        std::println(std::cout, "\nRecent ping records (last 10):");
        std::println(std::cout, "Timestamp           \tDelay\tStatus");
        std::println(std::cout, "--------------------------------------------------------");
    }

    void printRecentRecordRow(const std::string& timestamp, int delay, bool success) {
        std::println(std::cout, "{}\t{}ms\t{}", timestamp, delay, success ? "Success" : "Failed");
    }

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
