#include "utils.h"

std::map<std::string, std::string> readHostsFromFile(const std::string& filename) {
    std::map<std::string, std::string> hosts;
    std::ifstream file(filename);
    std::string line;
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return hosts;
    }
    
    while (std::getline(file, line)) {
        // 移除空行和注释行（以#开头）
        if (!line.empty() && line[0] != '#') {
            // 去除行首行尾空格
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            if (!line.empty()) {
                std::istringstream iss(line);
                std::string ip, hostname;
                if (iss >> ip >> hostname) {
                    hosts[ip] = hostname;
                }
            }
        }
    }
    
    file.close();
    return hosts;
}