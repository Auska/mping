#ifndef STATISTICS_PRINTER_H
#define STATISTICS_PRINTER_H

#include <iostream>
#include <print>
#include <string>

#include "database_base.h"

namespace StatisticsPrinter {

inline void printHeader(const std::string& ip, const std::string& hostname) {
    std::println(std::cout, "Statistics for IP: {} ({})", ip, hostname);
    std::println(std::cout, "=========================================================");
}

inline void printBody(const PingStatistics& stats) {
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

inline void printRecentRecordsHeader() {
    std::println(std::cout, "\nRecent ping records (last 10):");
    std::println(std::cout, "Timestamp           \tDelay\tStatus");
    std::println(std::cout, "--------------------------------------------------------");
}

inline void printRecentRecordRow(const std::string& timestamp, int delay, bool success) {
    std::println(std::cout, "{}\t{}ms\t{}", timestamp, delay, success ? "Success" : "Failed");
}

}  // namespace StatisticsPrinter

#endif  // STATISTICS_PRINTER_H
