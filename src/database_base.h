#ifndef DATABASE_BASE_H
#define DATABASE_BASE_H

#include <mutex>
#include <string>

#include "ip_validator.h"

// 数据库基类，提取公共逻辑
class DatabaseBase {
   protected:
    std::mutex dbMutex;  // 互斥锁，用于线程安全

    // 验证IP地址格式
    bool isValidIP(const std::string& ip) const {
        return IPValidator::isValidIPv4(ip);
    }

    DatabaseBase()          = default;
    virtual ~DatabaseBase() = default;

    // 禁止拷贝和移动
    DatabaseBase(const DatabaseBase&)            = delete;
    DatabaseBase& operator=(const DatabaseBase&) = delete;
    DatabaseBase(DatabaseBase&&)                 = delete;
    DatabaseBase& operator=(DatabaseBase&&)      = delete;

    // 允许测试访问 protected 成员
    friend class DatabaseBaseTest;
};

// 测试友元类
class DatabaseBaseTest : public DatabaseBase {
   public:
    static bool isValidIP(const std::string& ip) {
        DatabaseBaseTest instance;
        return instance.DatabaseBase::isValidIP(ip);
    }
};

#endif  // DATABASE_BASE_H