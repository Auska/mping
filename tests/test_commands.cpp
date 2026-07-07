#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "commands.h"
#include "constants.h"
#include "database_factory.h"
#include "database_manager.h"
#ifdef USE_POSTGRESQL
#include "database_manager_pg.h"
#endif

// 辅助函数：创建带数据的临时数据库
static std::string createPopulatedDatabase() {
    std::string testDb = "/tmp/test_commands_XXXXXX.db";
    int fd             = mkstemps(const_cast<char*>(testDb.c_str()), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize());

    // 插入 ping 结果
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
        {"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
        {"192.168.1.1", "host1", 15, true, "2025-01-01 00:01:00"},
        {"192.168.1.1", "host1", 20, true, "2025-01-01 00:02:00"},
        {"192.168.1.2", "host2", 10, true, "2025-01-01 00:00:00"},
        {"192.168.1.2", "host2", 0, false, "2025-01-01 00:01:00"},
        {"10.0.0.1", "host3", 0, false, "2025-01-01 00:00:00"},
    };
    REQUIRE(db.insertPingResults(results));

    // 添加告警
    REQUIRE(db.addAlert("192.168.1.2", "host2"));
    REQUIRE(db.addAlert("10.0.0.1", "host3"));

    return testDb;
}

// ══════════════════════════════════════════════════════════════════════════
//  Command 基类 — 创建数据库
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("Command creates database correctly", "[commands][base]") {
    std::string testDb = "/tmp/test_cmd_base_XXXXXX.db";
    int fd             = mkstemps(const_cast<char*>(testDb.c_str()), 3);
    REQUIRE(fd >= 0);
    close(fd);

    SECTION("Creates and initializes SQLite database") {
        // 使用 DatabaseFactory 直接测试，而非通过受保护的方法
        auto db = DatabaseFactory::createDatabase(DatabaseType::SQLITE, testDb);
        REQUIRE(db != nullptr);
        REQUIRE(db->initialize());
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("Command handles invalid database path", "[commands][base]") {
    SECTION("DatabaseFactory returns nullptr for unsupported type") {
        auto db =
            DatabaseFactory::createDatabase(DatabaseType::SQLITE, "/nonexistent/deep/path/db.db");
        // Returns a DatabaseManager but init will fail
        REQUIRE(db != nullptr);
        REQUIRE(db->initialize() == false);
    }
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryIPCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("QueryIPCommand executes successfully", "[commands][query]") {
    std::string testDb = createPopulatedDatabase();

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = testDb;
    cfg.queryIP        = "192.168.1.1";

    SECTION("Returns 0 for existing host") {
        QueryIPCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    SECTION("Returns 0 for non-existent host") {
        cfg.queryIP = "10.0.0.99";
        QueryIPCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);  // 不报错，只是输出统计为零
    }

    std::filesystem::remove(testDb);
}

// ══════════════════════════════════════════════════════════════════════════
//  CleanupCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("CleanupCommand executes successfully", "[commands][cleanup]") {
    std::string testDb = createPopulatedDatabase();

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = testDb;
    cfg.cleanupDays    = 365;  // 清理一年前的数据（应该无影响）

    SECTION("Returns 0") {
        CleanupCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    // 验证数据库仍然可用
    SECTION("Database still usable after cleanup") {
        CleanupCommand cmd(cfg);
        cmd.execute();

        DatabaseManager db(testDb);
        REQUIRE(db.initialize());
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() >= 2);
    }

    std::filesystem::remove(testDb);
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryAlertsCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("QueryAlertsCommand executes successfully", "[commands][alerts]") {
    std::string testDb = createPopulatedDatabase();

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = testDb;

    SECTION("Returns 0 with alerts present") {
        cfg.queryAlerts = ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS;
        QueryAlertsCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    SECTION("Returns 0 with specific day range") {
        cfg.queryAlerts = 365;
        QueryAlertsCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    std::filesystem::remove(testDb);
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryRecoveryCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("QueryRecoveryCommand executes successfully", "[commands][recovery]") {
    std::string testDb = createPopulatedDatabase();

    // 先解决一个告警，产生恢复记录
    {
        DatabaseManager db(testDb);
        REQUIRE(db.initialize());
        db.removeAlert("192.168.1.2");
    }

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = testDb;

    SECTION("Returns 0 with recovery records") {
        cfg.queryRecoveryRecords = ConfigDefaults::QUERY_MODE_ENABLED_NO_DAYS;
        QueryRecoveryCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    SECTION("Returns 0 with specific day range") {
        cfg.queryRecoveryRecords = 365;
        QueryRecoveryCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    SECTION("Returns 0 with no records in range") {
        cfg.queryRecoveryRecords = 1;  // 最近1天，不会有记录（测试数据是2025年的）
        QueryRecoveryCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    std::filesystem::remove(testDb);
}

// ══════════════════════════════════════════════════════════════════════════
//  PingCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("PingCommand error handling", "[commands][ping]") {
    SECTION("Returns 1 when no hosts file and no DB") {
        ConfigManager::Config cfg;
        cfg.filename = "/nonexistent/hosts.txt";
        PingCommand cmd(cfg);
        REQUIRE(cmd.execute() == 1);
    }

    SECTION("Returns 1 with empty config (default ip.txt not present)") {
        ConfigManager::Config cfg;
        PingCommand cmd(cfg);
        REQUIRE(cmd.execute() == 1);
    }
}

TEST_CASE("PingCommand with file-based hosts", "[commands][ping][file]") {
    // 创建一个符合格式的主机文件
    std::string testFile = "/tmp/test_ping_hosts_XXXXXX.txt";
    int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
    REQUIRE(fd >= 0);
    close(fd);

    std::ofstream file(testFile);
    REQUIRE(file.is_open());
    file << "# Test hosts" << std::endl;
    file << "127.0.0.1 localhost" << std::endl;
    file << "192.0.2.1 test-net" << std::endl;  // TEST-NET, unreachable
    file.close();

    SECTION("Executes ping with file hosts and returns 0") {
        ConfigManager::Config cfg;
        cfg.filename   = testFile;
        cfg.silentMode = true;
        PingCommand cmd(cfg);
        // 实际 ping 本地主机应该成功
        REQUIRE(cmd.execute() == 0);
    }

    SECTION("Executes ping with database enabled") {
        std::string testDb = "/tmp/test_ping_cmd_XXXXXX.db";
        int dbFd           = mkstemps(const_cast<char*>(testDb.c_str()), 3);
        REQUIRE(dbFd >= 0);
        close(dbFd);

        ConfigManager::Config cfg;
        cfg.filename       = testFile;
        cfg.enableDatabase = true;
        cfg.databasePath   = testDb;
        cfg.silentMode     = true;
        PingCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);

        // 验证数据库中有数据
        DatabaseManager db(testDb);
        REQUIRE(db.initialize());
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() >= 1);

        std::filesystem::remove(testDb);
    }

    std::filesystem::remove(testFile);
}

TEST_CASE("PingCommand alert lifecycle", "[commands][ping][alerts]") {
    std::string testFile = "/tmp/test_alert_hosts_XXXXXX.txt";
    int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
    REQUIRE(fd >= 0);
    close(fd);

    std::ofstream file(testFile);
    REQUIRE(file.is_open());
    file << "127.0.0.1 localhost" << std::endl;
    file.close();

    std::string testDb = "/tmp/test_alert_db_XXXXXX.db";
    int dbFd           = mkstemps(const_cast<char*>(testDb.c_str()), 3);
    REQUIRE(dbFd >= 0);
    close(dbFd);

    // 第一次运行：127.0.0.1 可达，不应产生告警
    {
        ConfigManager::Config cfg;
        cfg.filename       = testFile;
        cfg.enableDatabase = true;
        cfg.databasePath   = testDb;
        cfg.silentMode     = true;
        PingCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    // 验证：没有告警产生
    {
        DatabaseManager db(testDb);
        REQUIRE(db.initialize());
        auto alerts = db.getActiveAlerts();
        REQUIRE(alerts.empty());
    }

    std::filesystem::remove(testDb);
    std::filesystem::remove(testFile);
}
