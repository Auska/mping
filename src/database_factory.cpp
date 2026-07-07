#include "database_factory.h"
#ifdef USE_POSTGRESQL
#include "database_manager_pg.h"
#endif
#ifdef USE_SQLITE
#include "database_manager.h"
#endif
#include <memory>

std::unique_ptr<DatabaseInterface> DatabaseFactory::createDatabase(
    [[maybe_unused]] DatabaseType type, const std::string& connectionInfo) {
    if (type == DatabaseType::POSTGRESQL) {
#ifdef USE_POSTGRESQL
        return std::make_unique<DatabaseManagerPG>(connectionInfo);
#else
        return nullptr;
#endif
    }
#ifdef USE_SQLITE
    return std::make_unique<DatabaseManager>(connectionInfo);
#else
    return nullptr;
#endif
}

DatabaseType DatabaseFactory::detectDatabaseType(const std::string& connectionInfo) {
    // 检查连接字符串特征来判断数据库类型
    if (connectionInfo.find("host=") != std::string::npos
        || connectionInfo.find("port=") != std::string::npos
        || connectionInfo.find("user=") != std::string::npos
        || connectionInfo.find("password=") != std::string::npos
        || connectionInfo.find("dbname=") != std::string::npos) {
#ifdef USE_POSTGRESQL
        return DatabaseType::POSTGRESQL;
#else
        // 如果没有启用PostgreSQL支持，即使检测到PostgreSQL连接字符串也返回SQLite
        return DatabaseType::SQLITE;
#endif
    }
    return DatabaseType::SQLITE;
}