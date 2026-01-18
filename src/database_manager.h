#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <sqlite3.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "database_base.h"
#include "database_interface.h"

// 数据库管理类，用于处理SQLite数据库操作
class DatabaseManager : public DatabaseInterface, protected DatabaseBase {
   private:
    std::string dbPath;
    sqlite3* db;  // sqlite3* 类型的指针

    // 辅助函数：将IP地址转换为有效的表名
    std::string ipToTableName(const std::string& ip);

    // 为特定IP地址创建表
    bool createIPTable(const std::string& ip);

    // 验证并准备IP地址
    bool validateAndPrepareIPs(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 批量插入或更新主机信息
    bool upsertHosts(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 批量插入ping结果
    bool insertPingResultsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

   public:
    // 构造函数和析构函数
    explicit DatabaseManager(const std::string& path);
    ~DatabaseManager();

    // 初始化数据库
    bool initialize() override;

    // 插入单个ping结果
    bool insertPingResult(const std::string& ip, const std::string& hostname, short delay,
                          bool success, const std::string& timestamp) override;

    // 批量插入ping结果
    bool insertPingResults(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results)
        override;

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
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(
        int days = -1) override;

    // 获取恢复记录
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
    getRecoveryRecords(int days = -1) override;
};

#endif  // DATABASE_MANAGER_H