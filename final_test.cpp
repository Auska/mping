#include "database_manager.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <tuple>

int main() {
    try {
        // 使用测试数据库
        DatabaseManager db("final_test.db");
        
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        std::cout << "Database initialized successfully" << std::endl;
        
        // 测试添加多个告警
        std::cout << "Adding alerts..." << std::endl;
        db.addAlert("192.168.1.101", "Host1");
        db.addAlert("192.168.1.102", "Host2");
        
        // 模拟这些主机恢复
        std::cout << "Simulating host recovery..." << std::endl;
        db.removeAlert("192.168.1.101");
        db.removeAlert("192.168.1.102");
        
        // 查询恢复记录
        std::cout << "Testing getRecoveryRecords..." << std::endl;
        auto records = db.getRecoveryRecords();
        std::cout << "Found " << records.size() << " recovery records" << std::endl;
        
        for (const auto& [id, ip, hostname, alertTime, recoveryTime] : records) {
            std::cout << "Record ID: " << id << ", IP: " << ip << ", Hostname: " << hostname 
                      << ", Alert Time: " << alertTime << ", Recovery Time: " << recoveryTime << std::endl;
        }
        
        std::cout << "Final test completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}