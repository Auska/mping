#ifndef DATABASE_MANAGER_PG_H
#define DATABASE_MANAGER_PG_H

#include <libpq-fe.h>

#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

class SchemaMigrator;

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
    friend class SchemaMigrator;

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

    // 参数化查询执行器：params 为文本参数，内部构造 libpq 参数三件套（长度/格式），
    // 失败时与 PQexecParams 一致返回 nullptr（错误由调用方检查）
    PGresult* execParams(const std::string& sql, const std::vector<std::string>& params);

    // 连接数据库并配置会话（时区 UTC、消息级别、keepalive）；失败返回 false
    bool connectSession();

    // 建表 + 旧库迁移（幂等）；不预建分区
    bool prepareSchema();

    // 批量插入主机信息
    bool insertHostsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 批量插入ping结果
    bool insertPingResultsBatch(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results);

    // 单事务内参数化插入。先试单条多行 VALUES（一次往返）；分区表遇缺失分区
    // （SQLSTATE 23514）时回退到逐行 SAVEPOINT 插入，补建目标日分区后重试。
    // sqlPrefix 为 INSERT 头部，rowTemplate 为单行 VALUES 模板（可含 NOW()/::cast 等字面量，
    // 占位符 $N 在批量拼接时按行重编号），sqlSuffix 跟在行模板之后（如 ON CONFLICT 子句）；
    // serialize 把一行映射为参数文本列
    bool insertBatch(
        const char* sqlPrefix, const char* rowTemplate, const char* sqlSuffix,
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& rows,
        const std::function<
            void(const std::tuple<std::string, std::string, short, bool, std::string>&,
                 std::vector<std::string>&)>& serialize,
        bool partitionedInsert);

    // insertBatch 的 23514 回退路径：逐行 SAVEPOINT 插入，补建目标日分区后重试该行。
    // allParams 为 insertBatch 已序列化的行参数；失败时已回滚事务
    bool insertRowsWithSavepoints(
        const std::string& rowSQL,
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& rows,
        const std::vector<std::vector<std::string>>& allParams);

    // 查询执行器：days >= 0 时用 $1 参数化过滤，否则执行全部记录查询
    PGresult* executeOptionalDays(const char* sqlDays, const char* sqlAll, int days);

    // 删除 ping_results 表中超过 days 天的旧记录；返回删除行数，失败返回 -1
    int deleteOldPingResults(int days);

   public:
    // 构造函数和析构函数
    explicit DatabaseManagerPG(const std::string& connectionInfo);
    ~DatabaseManagerPG() = default;

    // 初始化数据库（连接 + 建表 + 迁移 + 预建未来分区；Ping 写入路径）
    bool initialize();

    // 仅查询路径的轻量初始化（连接 + 建表 + 迁移，不预建分区）
    bool initializeForQuery();

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

    // 批量新增告警（单条多行 UPSERT，ON CONFLICT DO NOTHING）；
    // 任一 IP 非法或插入失败返回 false（合法 IP 仍会写入，与原逐行语义一致）
    bool addAlerts(const std::vector<std::tuple<std::string, std::string>>& alerts);

    // 移除告警
    bool removeAlert(const std::string& ip);

    // 获取活动告警
    std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(int days = -1);

    // 获取恢复记录
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
    getRecoveryRecords(int days = -1);
};

#endif  // DATABASE_MANAGER_PG_H
