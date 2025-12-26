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
        
        // 查询所有恢复记录
        std::cout << "Testing getRecoveryRecords..." << std::endl;
        auto records = db.getRecoveryRecords();
        std::cout << "Found " << records.size() << " recovery records" << std::endl;
        
        for (const auto& [id, ip, hostname, alertTime, recoveryTime] : records) {
            std::cout << "Record ID: " << id << ", IP: " << ip << ", Hostname: " << hostname 
                      << ", Alert Time: " << alertTime << ", Recovery Time: " << recoveryTime << std::endl;
        }
        
        // 查询最近7天的恢复记录
        std::cout << "\nTesting getRecoveryRecords(7)..." << std::endl;
        auto recentRecords = db.getRecoveryRecords(7);
        std::cout << "Found " << recentRecords.size() << " recent recovery records" << std::endl;
        
        for (const auto& [id, ip, hostname, alertTime, recoveryTime] : recentRecords) {
            std::cout << "Record ID: " << id << ", IP: " << ip << ", Hostname: " << hostname 
                      << ", Alert Time: " << alertTime << ", Recovery Time: " << recoveryTime << std::endl;
        }
        
        std::cout << "All tests completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}