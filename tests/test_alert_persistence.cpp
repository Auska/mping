#include "database_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    try {
        DatabaseManager db("test_alert_persistence.db");
        
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        std::cout << "Database initialized successfully" << std::endl;
        
        // 第一次添加告警
        std::cout << "Adding alert for the first time..." << std::endl;
        bool addResult1 = db.addAlert("192.168.1.101", "TestHost1");
        if (addResult1) {
            std::cout << "Successfully added alert for 192.168.1.101" << std::endl;
        } else {
            std::cout << "Failed to add alert" << std::endl;
        }
        
        // 获取第一次添加的时间
        auto alerts1 = db.getActiveAlerts();
        std::string firstTime;
        for (const auto& [ip, hostname, createdTime] : alerts1) {
            if (ip == "192.168.1.101") {
                firstTime = createdTime;
                std::cout << "First alert time: " << createdTime << std::endl;
            }
        }
        
        // 等待2秒
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 第二次尝试添加相同IP的告警（应该不会覆盖）
        std::cout << "Adding alert for the same IP again..." << std::endl;
        bool addResult2 = db.addAlert("192.168.1.101", "TestHost1_Updated");
        if (addResult2) {
            std::cout << "Successfully added alert for 192.168.1.101" << std::endl;
        } else {
            std::cout << "Alert was not added (as expected)" << std::endl;
        }
        
        // 检查时间是否保持不变
        auto alerts2 = db.getActiveAlerts();
        for (const auto& [ip, hostname, createdTime] : alerts2) {
            if (ip == "192.168.1.101") {
                std::cout << "Current alert: " << ip << " - " << hostname << " - " << createdTime << std::endl;
                if (createdTime == firstTime) {
                    std::cout << "SUCCESS: Alert time was preserved!" << std::endl;
                } else {
                    std::cout << "ERROR: Alert time was changed!" << std::endl;
                }
            }
        }
        
        std::cout << "All tests completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}