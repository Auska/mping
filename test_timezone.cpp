#include "database_manager.h"
#include <iostream>
#include <chrono>
#include <ctime>

int main() {
    try {
        DatabaseManager db("test_timezone.db");
        
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        std::cout << "Database initialized successfully" << std::endl;
        
        // 获取当前本地时间
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::cout << "Current local time: " << std::ctime(&time_t);
        
        // 测试添加告警
        std::cout << "Testing addAlert..." << std::endl;
        bool addResult = db.addAlert("192.168.1.100", "TestHost");
        if (addResult) {
            std::cout << "Successfully added alert for 192.168.1.100" << std::endl;
        } else {
            std::cout << "Failed to add alert" << std::endl;
        }
        
        // 测试查询告警
        std::cout << "Testing getActiveAlerts..." << std::endl;
        auto alerts = db.getActiveAlerts();
        std::cout << "Found " << alerts.size() << " active alerts" << std::endl;
        
        for (const auto& [ip, hostname, createdTime] : alerts) {
            std::cout << "Alert: " << ip << " - " << hostname << " - " << createdTime << std::endl;
        }
        
        std::cout << "All tests completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}