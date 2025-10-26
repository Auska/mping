#include "print_results.h"
#include <iostream>
#include <iomanip>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] [filename]\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help\t\tShow this help message\n";
    std::cout << "  -d, --database\tEnable database logging (default: enabled)\n";
    std::cout << "  -f, --file\t\tSpecify input file with hosts (default: ip.txt)\n";
    std::cout << "  -q, --query\t\tQuery statistics for a specific IP address\n";
    std::cout << "  -c, --consecutive-failures [n]\tQuery hosts with n consecutive failures (default: 3)\n";
    std::cout << "Default filename: ip.txt\n";
    std::cout << "Default behavior: Show all hosts with status (IP, hostname, status, delay)\n";
}

void printSuccessfulHosts(const std::vector<std::tuple<std::string, std::string, long long>>& successfulHosts) {
    if (!successfulHosts.empty()) {
        for (const auto& [ip, hostname, delay] : successfulHosts) {
            std::cout << ip << "\t" << hostname << "\t" << delay << "ms" << std::endl;
        }
    }
}

void printFailedHosts(const std::vector<std::pair<std::string, std::string>>& failedHosts) {
    if (!failedHosts.empty()) {
        for (const auto& [ip, hostname] : failedHosts) {
            std::cout << ip << "\t" << hostname << std::endl;
        }
    }
}