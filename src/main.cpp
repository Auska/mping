#include <exception>
#include <iostream>
#include <memory>
#include <print>

#include "commands.h"
#include "config_manager.h"
#include "constants.h"

namespace {

std::unique_ptr<Command> dispatchCommand(const ConfigManager::Config& config) {
    if (!config.queryIP.empty()) {
        return std::make_unique<QueryIPCommand>(config);
    }
    // cleanup 作为一次性命令仅在非持续模式下生效；持续模式（check_interval > 0）下
    // cleanup_days 由 PingCommand 循环内每轮执行，避免清理后直接退出
    if (config.cleanupDays >= 0 && config.checkIntervalSeconds <= 0) {
        return std::make_unique<CleanupCommand>(config);
    }
    // queryAlerts/queryRecoveryRecords: -1 = disabled (default), -2 = enabled without days,
    // >= 0 = enabled with days
    if (config.queryAlerts != -1) {
        return std::make_unique<QueryAlertsCommand>(config);
    }
    if (config.queryRecoveryRecords != -1) {
        return std::make_unique<QueryRecoveryCommand>(config);
    }
    return std::make_unique<PingCommand>(config);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        ConfigManager configManager;
        int exitCode = 0;
        if (!configManager.parseArguments(argc, argv, &exitCode)) {
            return exitCode;
        }
        return dispatchCommand(configManager.getConfig())->execute();
    } catch (const std::exception& e) {
        std::println(std::cerr, "Exception occurred: {}", e.what());
        return 1;
    } catch (...) {
        std::println(std::cerr, "Unknown exception occurred");
        return 1;
    }
}
