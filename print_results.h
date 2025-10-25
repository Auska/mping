#ifndef PRINT_RESULTS_H
#define PRINT_RESULTS_H

#include <string>
#include <vector>
#include <tuple>

// 打印使用帮助
void printUsage(const char* programName);

// 打印成功的主机
void printSuccessfulHosts(const std::vector<std::tuple<std::string, std::string, long long>>& successfulHosts);

// 打印失败的主机
void printFailedHosts(const std::vector<std::pair<std::string, std::string>>& failedHosts);

#endif // PRINT_RESULTS_H