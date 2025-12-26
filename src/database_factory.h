#ifndef DATABASE_FACTORY_H
#define DATABASE_FACTORY_H

#include "database_interface.h"
#include <string>
#include <memory>

enum class DatabaseType {
    SQLITE,
    POSTGRESQL
};

class DatabaseFactory {
public:
    static std::unique_ptr<DatabaseInterface> createDatabase(DatabaseType type, const std::string& connectionInfo);
    
    // 自动检测数据库类型
    static DatabaseType detectDatabaseType(const std::string& connectionInfo);
};

#endif // DATABASE_FACTORY_H