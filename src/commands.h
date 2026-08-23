#ifndef COMMANDS_H
#define COMMANDS_H

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "config_manager.h"

class DatabaseManagerPG;

/**
 * @brief 命令抽象基类
 *
 * 将 main() 中的每种操作模式封装为独立命令：
 * - QueryIPCommand     查询 IP 统计
 * - CleanupCommand     清理旧数据
 * - QueryAlertsCommand 查询告警
 * - QueryRecoveryCommand 查询恢复记录
 * - PingCommand        执行 Ping 并存储结果
 */
class Command {
   public:
    virtual ~Command()    = default;
    virtual int execute() = 0;

   protected:
    const ConfigManager::Config& config;

    explicit Command(const ConfigManager::Config& cfg);

    // 根据配置和编译选项创建数据库实例
    std::unique_ptr<DatabaseManagerPG> createDatabase();
    // precreatePartitions=false：仅查询命令使用，跳过未来分区预建（不写入）
    bool initializeDatabase(DatabaseManagerPG& db, bool precreatePartitions = true);
};

// ─── 查询 IP 统计 ────────────────────────────────────────────────────────
class QueryIPCommand : public Command {
   public:
    explicit QueryIPCommand(const ConfigManager::Config& cfg) : Command(cfg) {}
    int execute() override;
};

// ─── 清理旧数据 ───────────────────────────────────────────────────────────
class CleanupCommand : public Command {
   public:
    explicit CleanupCommand(const ConfigManager::Config& cfg) : Command(cfg) {}
    int execute() override;
};

// ─── 查询告警 ─────────────────────────────────────────────────────────────
class QueryAlertsCommand : public Command {
   public:
    explicit QueryAlertsCommand(const ConfigManager::Config& cfg) : Command(cfg) {}
    int execute() override;
};

// ─── 查询恢复记录 ─────────────────────────────────────────────────────────
class QueryRecoveryCommand : public Command {
   public:
    explicit QueryRecoveryCommand(const ConfigManager::Config& cfg) : Command(cfg) {}
    int execute() override;
};

// ─── Ping 结果 ───────────────────────────────────────────────────────────
struct PingResult {
    std::string ip;
    std::string hostname;
    bool success  = false;
    short delayMs = 0;
    std::string timestamp;
};

// ─── 执行 Ping ────────────────────────────────────────────────────────────

class PingCommand : public Command {
   public:
    explicit PingCommand(const ConfigManager::Config& cfg) : Command(cfg) {}
    int execute() override;

   private:
    // 加载主机列表：-f 文件 > 数据库 hosts 表 > 默认 ip.txt
    bool loadHosts(std::unique_ptr<DatabaseManagerPG>& db,
                   std::map<std::string, std::string>& hosts);
    // 落库、告警处理与自动清理（仅数据库模式调用）
    bool persistResults(std::vector<PingResult>& allResults, std::unique_ptr<DatabaseManagerPG>& db,
                        const std::unordered_set<std::string>& alertIPs);
    // 打印结果（静默模式跳过）
    void printResults(const std::vector<PingResult>& allResults);
    // 数据库是否启用：仅由配置文件 database_path 决定；查询/清理命令无条件使用数据库
    bool useDatabase() const { return config.databasePathSet; }
    bool insertPingResults(DatabaseManagerPG* db, const std::vector<PingResult>& allResults);
    bool processAlerts(DatabaseManagerPG* db, const std::vector<PingResult>& allResults,
                       const std::unordered_set<std::string>& alertIPs);
};

#endif  // COMMANDS_H
