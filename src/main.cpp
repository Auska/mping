#include <exception>
#include <iostream>
#include <print>

#include "commands.h"
#include "config_manager.h"
#include "constants.h"

int main(int argc, char* argv[]) {
    try {
        // 解析命令行参数
        ConfigManager configManager;
        if (!configManager.parseArguments(argc, argv)) {
            return 0;
        }

        const auto& config = configManager.getConfig();

        // ── 命令调度 ──────────────────────────────────────────────────
        // 每种操作模式由独立的 Command 子类处理
        std::unique_ptr<Command> cmd;

        if (!config.queryIP.empty()) {
            cmd = std::make_unique<QueryIPCommand>(config);
        } else if (config.cleanupDays >= 0) {
            cmd = std::make_unique<CleanupCommand>(config);
        } else if (config.queryAlerts >= 0
                   || config.queryAlerts == ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS) {
            cmd = std::make_unique<QueryAlertsCommand>(config);
        } else if (config.queryRecoveryRecords >= 0
                   || config.queryRecoveryRecords
                          == ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS) {
            cmd = std::make_unique<QueryRecoveryCommand>(config);
        } else {
            cmd = std::make_unique<PingCommand>(config);
        }

        return cmd->execute();

    } catch (const std::exception& e) {
        std::println(std::cerr, "Exception occurred: {}", e.what());
        return 1;
    } catch (...) {
        std::println(std::cerr, "Unknown exception occurred");
        return 1;
    }
}
