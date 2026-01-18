#ifndef DATABASE_FACTORY_H
#define DATABASE_FACTORY_H

#include <memory>
#include <string>

#include "database_interface.h"

enum class DatabaseType { SQLITE, POSTGRESQL };

class DatabaseFactory {
   public:
    static std::unique_ptr<DatabaseInterface> createDatabase(DatabaseType type,
                                                             const std::string& connectionInfo);

    // 自动检测数据库类型
    static DatabaseType detectDatabaseType(const std::string& connectionInfo);
};

#endif  // DATABASE_FACTORY_H