#include "database_manager_pg.h"
#include <iostream>
#include <vector>
#include <tuple>

int main() {
    // PostgreSQL连接字符串示例
    std::string connInfo = "host=localhost user=mping_user password=mping_pass dbname=mping_test";
    
    // 创建数据库管理器实例
    DatabaseManagerPG db(connInfo);
    
    // 初始化数据库
    if (!db.initialize()) {
        std::cerr << "Failed to initialize database" << std::endl;
        return 1;
    }
    
    std::cout << "Database initialized successfully" << std::endl;
    
    // 插入一些测试数据
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results;
    results.emplace_back("192.168.1.1", "testhost1", 10, true, "2023-01-01 10:00:00");
    results.emplace_back("192.168.1.2", "testhost2", 20, false, "2023-01-01 10:00:05");
    results.emplace_back("192.168.1.3", "testhost3", 15, true, "2023-01-01 10:01:00");
    
    if (!db.insertPingResults(results)) {
        std::cerr << "Failed to insert ping results" << std::endl;
        return 1;
    }
    
    std::cout << "Ping results inserted successfully" << std::endl;
    
    // 查询统计数据
    std::cout << "\n--- Querying statistics for 192.168.1.1 ---" << std::endl;
    db.queryIPStatistics("192.168.1.1");
    
    // 获取所有主机
    std::cout << "\n--- Getting all hosts ---" << std::endl;
    auto hosts = db.getAllHosts();
    for (const auto& [ip, hostname] : hosts) {
        std::cout << ip << " -> " << hostname << std::endl;
    }
    
    return 0;
}