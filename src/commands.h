#ifndef COMMANDS_H
#define COMMANDS_H

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "config_manager.h"

class DatabaseInterface;

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
    std::unique_ptr<DatabaseInterface> createDatabase();
    bool initializeDatabase(DatabaseInterface* db);
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

// ─── 执行 Ping ────────────────────────────────────────────────────────────
using PingResult = std::tuple<std::string, std::string, bool, short, std::string>;

class PingCommand : public Command {
   public:
    explicit PingCommand(const ConfigManager::Config& cfg) : Command(cfg) {}
    int execute() override;

   private:
    bool insertPingResults(DatabaseInterface* db, const std::vector<PingResult>& allResults);
    bool processAlerts(DatabaseInterface* db, const std::vector<PingResult>& allResults);
};

#endif  // COMMANDS_H
