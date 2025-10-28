#!/bin/bash

# PostgreSQL测试脚本

# 创建测试数据库和用户
echo "Creating test database and user..."
sudo -u postgres psql << EOF
CREATE DATABASE mping_test;
CREATE USER mping_user WITH PASSWORD 'mping_pass';
GRANT ALL PRIVILEGES ON DATABASE mping_test TO mping_user;
\c mping_test
GRANT ALL ON SCHEMA public TO mping_user;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO mping_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO mping_user;
EOF

# 测试PostgreSQL连接
echo "Testing PostgreSQL connection..."
cd /home/code/mping/build
./test_pg

# 清理测试数据库和用户
echo "Cleaning up test database and user..."
sudo -u postgres psql << EOF
DROP DATABASE IF EXISTS mping_test;
DROP USER IF EXISTS mping_user;
EOF

echo "PostgreSQL test completed."
