#include "utils.h"

#include <print>
#include <regex>
#include <stdexcept>

std::map<std::string, std::string> readHostsFromFile(const std::string& filename) {
    std::map<std::string, std::string> hosts;

    if (filename.empty()) {
        throw std::invalid_argument("Filename cannot be empty");
    }

    std::ifstream file(filename);

    if (!file.is_open()) {
        std::println(std::cerr, "Failed to open file: {}", filename);
        return hosts;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;
        // 移除空行和注释行（以#开头）
        if (!line.empty() && line[0] != '#') {
            // 去除行首行尾空格
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            if (!line.empty()) {
                std::istringstream iss(line);
                std::string ip, hostname;
                if (iss >> ip >> hostname) {
                    // 验证 IP 格式
                    std::regex ipPattern(
                        R"((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?))");
                    if (!std::regex_match(ip, ipPattern)) {
                        std::println(std::cerr,
                                     "Warning: Invalid IP format on line {} in file {}: {}",
                                     lineNumber, filename, ip);
                        continue;
                    }
                    hosts[ip] = hostname;
                } else {
                    std::println(std::cerr, "Warning: Invalid format on line {} in file {}",
                                 lineNumber, filename);
                }
            }
        }
    }

    // 文件会在析构时自动关闭，但显式关闭是一个好习惯
    file.close();
    return hosts;
}