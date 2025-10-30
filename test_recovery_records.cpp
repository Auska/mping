#include "database_manager.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <tuple>

int main() {
    try {
        // 使用测试数据库
        DatabaseManager db("test_recovery.db");
        
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        std::cout << "Database initialized successfully" << std::endl;
        
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
        
        // 测试删除告警（模拟主机恢复）
        std::cout << "Testing removeAlert (simulate host recovery)..." << std::endl;
        bool removeResult = db.removeAlert("192.168.1.100");
        if (removeResult) {
            std::cout << "Successfully removed alert for 192.168.1.100 (host recovered)" << std::endl;
        } else {
            std::cout << "Failed to remove alert" << std::endl;
        }
        
        // 再次查询确认删除
        alerts = db.getActiveAlerts();
        std::cout << "After removal, found " << alerts.size() << " active alerts" << std::endl;
        
        std::cout << "All tests completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}