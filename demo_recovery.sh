#!/bin/bash

# 演示恢复记录功能的测试脚本

echo "=== 演示恢复记录功能 ==="

# 创建测试目录
cd /home/code/mping/build

# 创建测试主机文件
cat > demo_hosts.txt << EOF
8.8.8.8     Google DNS
114.114.114.114  114 DNS
192.168.1.1      Router
192.168.1.999    Invalid IP (用于测试告警)
EOF

echo "Created demo_hosts.txt with sample hosts"

# 使用SQLite3数据库测试
echo "Testing with SQLite3 database..."
DB_NAME="demo_recovery.db"

# 清理之前的测试数据库
rm -f $DB_NAME

# 执行ping操作并记录结果到SQLite3数据库
echo "执行ping操作并记录到SQLite3数据库..."
./mping -d $DB_NAME -f demo_hosts.txt -n 1 -t 1

# 查询告警
echo "查询当前告警..."
./mping -d $DB_NAME -a

# 查询恢复记录
echo "查询恢复记录..."
./mping -d $DB_NAME -r

# 再次执行ping操作，模拟主机恢复
echo "再次执行ping操作，模拟主机恢复..."
./mping -d $DB_NAME -f demo_hosts.txt -n 1 -t 1

# 再次查询告警和恢复记录
echo "再次查询告警..."
./mping -d $DB_NAME -a

echo "再次查询恢复记录..."
./mping -d $DB_NAME -r

# 查询最近1天的恢复记录
echo "查询最近1天的恢复记录..."
./mping -d $DB_NAME -r 1

# 清理测试文件
rm -f $DB_NAME demo_hosts.txt

echo "演示完成！"