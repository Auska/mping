#ifndef DATABASE_MANAGER_PG_H
#define DATABASE_MANAGER_PG_H

#include "database_interface.h"
#include <string>
#include <vector>
#include <tuple>
#include <map>
#include <mutex>
#include <libpq-fe.h>

// 数据库管理类，用于处理PostgreSQL数据库操作
class DatabaseManagerPG : public DatabaseInterface {
private:
    std::string connInfo;
    PGconn* conn;
    std::mutex dbMutex;  // 互斥锁，用于线程安全
    
    // 验证IP地址格式
    bool isValidIP(const std::string& ip);
    
    // 转义字符串以防止SQL注入
    std::string escapeString(const std::string& str);
    
    // 执行不返回结果的查询
    bool executeQuery(const std::string& query);
    
    // 执行返回结果的查询
    PGresult* executeQueryWithResult(const std::string& query);
    
    // 检查数据库连接状态
    bool checkConnection();
    
    // 验证IP地址格式
    bool validateIPs(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    
    // 为特定IP地址创建表
    bool createIPTables(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    
    // 批量插入主机信息
    bool insertHostsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);
    
    // 批量插入ping结果
    bool insertPingResultsBatch(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

public:
    // 构造函数和析构函数
    explicit DatabaseManagerPG(const std::string& connectionInfo);
    ~DatabaseManagerPG();
    
    // 初始化数据库
    bool initialize() override;
    
    // 插入单个ping结果
    bool insertPingResult(const std::string& ip, const std::string& hostname, short delay, bool success, const std::string& timestamp) override;
    
    // 批量插入ping结果
    bool insertPingResults(const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) override;
    
    // 查询IP统计信息
    void queryIPStatistics(const std::string& ip) override;
    
    // 清理旧数据
    void cleanupOldData(int days) override;
    
    // 获取所有主机
    std::map<std::string, std::string> getAllHosts() override;
    
    // 添加告警
    bool addAlert(const std::string& ip, const std::string& hostname) override;
    
    // 移除告警
    bool removeAlert(const std::string& ip) override;
    
    // 获取活动告警
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(int days = -1) override;
    
    // 获取恢复记录
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> getRecoveryRecords(int days = -1) override;
};

#endif // DATABASE_MANAGER_PG_H