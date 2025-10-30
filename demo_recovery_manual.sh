#!/bin/bash

# 演示恢复记录功能的测试脚本

echo "=== 演示恢复记录功能 ==="

cd /home/code/mping/build

# 数据库名称
DB_NAME="demo_recovery.db"

# 清理之前的测试数据库
rm -f $DB_NAME

# 初始化数据库
echo "初始化数据库..."
./mping -d $DB_NAME

# 手动添加一个告警记录
echo "手动添加一个告警记录..."
sqlite3 $DB_NAME "INSERT INTO alerts (ip, hostname, created_time) VALUES ('192.168.1.100', 'TestHost', '2025-10-30 10:00:00');"

# 查询当前告警
echo "查询当前告警..."
./mping -d $DB_NAME -a

# 查询恢复记录（应该为空）
echo "查询恢复记录（应该为空）..."
./mping -d $DB_NAME -r

# 模拟主机恢复（从告警表中移除记录，这将自动创建恢复记录）
echo "模拟主机恢复..."
sqlite3 $DB_NAME << EOF
.headers on
.mode column
.tables
.schema recovery_records
EOF

# 手动测试removeAlert功能
echo "创建测试程序来验证removeAlert功能..."

cat > test_manual_recovery.cpp << EOF
#include "database_manager.h"
#include <iostream>

int main() {
    try {
        DatabaseManager db("demo_recovery.db");
        
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        std::cout << "Database initialized successfully" << std::endl;
        
        // 查询当前告警
        std::cout << "查询当前告警..." << std::endl;
        auto alerts = db.getActiveAlerts();
        std::cout << "Found " << alerts.size() << " active alerts" << std::endl;
        
        for (const auto& [ip, hostname, createdTime] : alerts) {
            std::cout << "Alert: " << ip << " - " << hostname << " - " << createdTime << std::endl;
        }
        
        // 移除告警（模拟主机恢复）
        std::cout << "移除告警（模拟主机恢复）..." << std::endl;
        bool result = db.removeAlert("192.168.1.100");
        if (result) {
            std::cout << "Successfully removed alert for 192.168.1.100" << std::endl;
        } else {
            std::cout << "Failed to remove alert" << std::endl;
        }
        
        // 再次查询告警
        std::cout << "再次查询告警..." << std::endl;
        alerts = db.getActiveAlerts();
        std::cout << "Found " << alerts.size() << " active alerts" << std::endl;
        
        // 查询恢复记录
        std::cout << "查询恢复记录..." << std::endl;
        auto records = db.getRecoveryRecords();
        std::cout << "Found " << records.size() << " recovery records" << std::endl;
        
        for (const auto& [id, ip, hostname, alertTime, recoveryTime] : records) {
            std::cout << "Record ID: " << id << ", IP: " << ip << ", Hostname: " << hostname 
                      << ", Alert Time: " << alertTime << ", Recovery Time: " << recoveryTime << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
EOF

# 编译测试程序
echo "编译测试程序..."
g++ -std=c++20 -O2 -I.. -I../build test_manual_recovery.cpp ../build/libmping.a -lsqlite3 -lpthread -o test_manual_recovery

# 运行测试程序
echo "运行测试程序..."
./test_manual_recovery

# 清理
rm -f test_manual_recovery test_manual_recovery.cpp

echo "演示完成！"