#ifndef DATABASE_MANAGER_PG_H
#define DATABASE_MANAGER_PG_H

#include <libpq-fe.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

// 单个 IP 的 ping 统计结果
struct PingStatistics {
    int totalRecords   = 0;
    int successCount   = 0;
    int failureCount   = 0;
    int maxDelay       = 0;
    int minDelay       = 0;
    double avgDelay    = 0;
    double successRate = 0;
    double failureRate = 0;
};

// PostgreSQL 数据库操作类（唯一数据库后端）
class DatabaseManagerPG {
   private:
    std::string connInfo;
    std::mutex dbMutex;
    // 使用智能指针管理 PGconn 连接
    struct PGconnDeleter {
        void operator()(PGconn* conn) const {
            if (conn) {
                PQfinish(conn);
            }
        }
    };
    std::unique_ptr<PGconn, PGconnDeleter> conn;

    // 检查连接状态，断开时自动重连；成功返回 true
    bool ensureConnected();

    // 执行不返回结果的查询
    bool executeQuery(const std::string& query);

    // 执行返回结果的查询
    PGresult* executeQueryWithResult(const std::string& query);

    // 批量插入主机信息
    bool insertHostsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 批量插入ping结果
    bool insertPingResultsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 在单事务内逐行执行参数化插入；serialize 把一行映射为参数文本列
    bool insertBatch(
        const char* sql,
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& rows,
        const std::function<
            void(const std::tuple<std::string, std::string, short, bool, std::string>&,
                 std::vector<std::string>&)>& serialize);

    // 将旧 TIMESTAMP 列迁移为 TIMESTAMPTZ
    bool migrateSchema();

    // 查询执行器：days >= 0 时用 $1 参数化过滤，否则执行全部记录查询
    PGresult* executeOptionalDays(const char* sqlDays, const char* sqlAll, int days);

    // 删除 ping_results 表中超过 days 天的旧记录；返回删除行数，失败返回 -1
    int deleteOldPingResults(int days);

   public:
    // 构造函数和析构函数
    explicit DatabaseManagerPG(const std::string& connectionInfo);
    ~DatabaseManagerPG() = default;

    // 初始化数据库
    bool initialize();

    // 插入单个ping结果
    bool insertPingResult(const std::string& ip, const std::string& hostname, short delay,
                          bool success, const std::string& timestamp);

    // 批量插入ping结果
    bool insertPingResults(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 查询IP统计信息
    void queryIPStatistics(const std::string& ip);

    // 拆分后的私有查询方法
    std::string queryHostName(const std::string& ip);
    PingStatistics queryStatistics(const std::string& ip);
    void printRecentRecords(const std::string& ip);

    // 清理旧数据
    void cleanupOldData(int days);

    // 仅清理ping_results表中的旧数据
    void cleanupOldPingResults(int days);

    // 获取所有主机
    std::map<std::string, std::string> getAllHosts();

    // 添加告警
    bool addAlert(const std::string& ip, const std::string& hostname);

    // 移除告警
    bool removeAlert(const std::string& ip);

    // 获取活动告警
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(int days = -1);

    // 获取恢复记录
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
    getRecoveryRecords(int days = -1);
};

#endif  // DATABASE_MANAGER_PG_H
