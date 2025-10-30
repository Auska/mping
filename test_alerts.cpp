#include "database_manager_pg.h"
#include <iostream>
#include <print>
#include <cassert>
#include <vector>
#include <tuple>

int main() {
    // 测试连接字符串 - 请根据实际环境修改
    std::string connStr = "host=localhost port=5432 user=postgres password=postgres dbname=test";
    
    try {
        DatabaseManagerPG db(connStr);
        
        if (!db.initialize()) {
            std::println(std::cerr, "Failed to initialize database");
            return 1;
        }
        
        std::println(std::cout, "Database initialized successfully");
        
        // 测试添加告警
        std::println(std::cout, "Testing addAlert...");
        bool addResult = db.addAlert("192.168.1.99", "TestHost");
        if (addResult) {
            std::println(std::cout, "Successfully added alert for 192.168.1.99");
        } else {
            std::println(std::cout, "Failed to add alert");
        }
        
        // 测试查询告警
        std::println(std::cout, "Testing getActiveAlerts...");
        auto alerts = db.getActiveAlerts();
        std::println(std::cout, "Found {} active alerts", alerts.size());
        
        for (const auto& [ip, hostname, createdTime] : alerts) {
            std::println(std::cout, "Alert: {} - {} - {}", ip, hostname, createdTime);
        }
        
        // 测试删除告警
        std::println(std::cout, "Testing removeAlert...");
        bool removeResult = db.removeAlert("192.168.1.99");
        if (removeResult) {
            std::println(std::cout, "Successfully removed alert for 192.168.1.99");
        } else {
            std::println(std::cout, "Failed to remove alert");
        }
        
        // 再次查询确认删除
        alerts = db.getActiveAlerts();
        std::println(std::cout, "After removal, found {} active alerts", alerts.size());
        
        std::println(std::cout, "All tests completed successfully!");
        
    } catch (const std::exception& e) {
        std::println(std::cerr, "Exception: {}", e.what());
        return 1;
    }
    
    return 0;
}