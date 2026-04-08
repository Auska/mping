#ifndef DATABASE_MANAGER_PG_H
#define DATABASE_MANAGER_PG_H

#include <libpq-fe.h>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "database_base.h"
#include "database_interface.h"

// 数据库管理类，用于处理PostgreSQL数据库操作
class DatabaseManagerPG : public DatabaseInterface, protected DatabaseBase {
   private:
    std::string connInfo;
    // 使用智能指针管理 PGconn 连接
    struct PGconnDeleter {
        void operator()(PGconn* conn) const {
            if (conn) {
                PQfinish(conn);
            }
        }
    };
    std::unique_ptr<PGconn, PGconnDeleter> conn;

    // 转义字符串以防止SQL注入
    std::string escapeString(const std::string& str);

    // 执行不返回结果的查询
    bool executeQuery(const std::string& query);

    // 执行返回结果的查询
    PGresult* executeQueryWithResult(const std::string& query);

    // 检查数据库连接状态
    bool checkConnection();

    // 为特定IP地址创建表
    bool createIPTables(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 批量插入主机信息
    bool insertHostsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 批量插入ping结果
    bool insertPingResultsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

   public:
    // 构造函数和析构函数
    explicit DatabaseManagerPG(const std::string& connectionInfo);
    ~DatabaseManagerPG();

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

#endif  // DATABASE_MANAGER_PG_H