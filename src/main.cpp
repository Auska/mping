#include <exception>
#include <iostream>
#include <memory>
#include <print>

#include "commands.h"
#include "config_manager.h"
#include "constants.h"

namespace {

bool isQueryModeEnabled(int val) {
    // -1 = disabled (default), -2 = enabled without days, >= 0 = enabled with days
    return val != -1;
}

std::unique_ptr<Command> dispatchCommand(const ConfigManager::Config& config) {
    if (!config.queryIP.empty()) {
        return std::make_unique<QueryIPCommand>(config);
    }
    if (config.cleanupDays >= 0) {
        return std::make_unique<CleanupCommand>(config);
    }
    if (isQueryModeEnabled(config.queryAlerts)) {
        return std::make_unique<QueryAlertsCommand>(config);
    }
    if (isQueryModeEnabled(config.queryRecoveryRecords)) {
        return std::make_unique<QueryRecoveryCommand>(config);
    }
    return std::make_unique<PingCommand>(config);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        ConfigManager configManager;
        if (!configManager.parseArguments(argc, argv)) {
            return 0;
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
