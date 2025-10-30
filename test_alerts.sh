#!/bin/bash

# 测试脚本用于验证SQLite3和PostgreSQL的告警功能

echo "=== SQLite3 告警功能测试 ==="

# 创建测试主机文件
cat > test_hosts.txt << EOF
8.8.8.8     Google DNS
114.114.114.114  114 DNS
192.168.1.1      Router
192.168.1.999    Invalid IP (用于测试告警)
EOF

echo "Created test_hosts.txt with sample hosts"

# 使用SQLite3数据库测试
echo "Testing with SQLite3 database..."
cd /home/code/mping/build

# 执行ping操作并记录结果到SQLite3数据库
echo "执行ping操作并记录到SQLite3数据库..."
./mping -d test.db -f ../test_hosts.txt -n 1 -t 1

# 查询告警
echo "查询当前告警..."
./mping -d test.db --alerts

# 清理测试文件
rm -f test.db test_hosts.txt

echo ""
echo "=== PostgreSQL 告警功能测试 ==="
echo "要测试PostgreSQL告警功能，请确保PostgreSQL服务器正在运行，然后执行以下命令："
echo "./mping -P -d 'host=localhost user=youruser password=yourpass dbname=yourdb' -f ../test_hosts.txt"
echo "./mping -P -d 'host=localhost user=youruser password=yourpass dbname=yourdb' --alerts"