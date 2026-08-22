#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "commands.h"
#include "constants.h"
#include "database_manager_pg.h"
#include "test_helpers.h"

// 需要数据库的命令测试依赖真实 PostgreSQL 服务：
// 通过环境变量 MPING_TEST_PG_CONNSTR 指定连接串（见 test_helpers.h），
// 服务不可达时对应测试自动跳过（SKIP），不影响其余测试。
//
//   MPING_TEST_PG_CONNSTR='host=localhost user=postgres dbname=mping_test' ./build/mping_tests

// 在测试数据库中建表并填充数据，返回连接串（调用方需先确认 pgAvailable()）
static std::string createPopulatedDatabase() {
    auto db = std::make_unique<DatabaseManagerPG>(testPgConnstr());
    REQUIRE(db != nullptr);
    REQUIRE(db->initialize());

    // 插入 ping 结果
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
        {"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
        {"192.168.1.1", "host1", 15, true, "2025-01-01 00:01:00"},
        {"192.168.1.1", "host1", 20, true, "2025-01-01 00:02:00"},
        {"192.168.1.2", "host2", 10, true, "2025-01-01 00:00:00"},
        {"192.168.1.2", "host2", 0, false, "2025-01-01 00:01:00"},
        {"10.0.0.1", "host3", 0, false, "2025-01-01 00:00:00"},
    };
    REQUIRE(db->insertPingResults(results));

    // 添加告警
    REQUIRE(db->addAlert("192.168.1.2", "host2"));
    REQUIRE(db->addAlert("10.0.0.1", "host3"));

    return testPgConnstr();
}

// ══════════════════════════════════════════════════════════════════════════
//  Command 基类 — 创建数据库
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("Command creates database correctly", "[commands][base]") {
    auto db = std::make_unique<DatabaseManagerPG>(testPgConnstr());
    REQUIRE(db != nullptr);
}

TEST_CASE("Command handles invalid database connection", "[commands][base]") {
    SECTION("Empty connection string throws") {
        REQUIRE_THROWS_AS(std::make_unique<DatabaseManagerPG>(""), std::invalid_argument);
    }

    SECTION("Unreachable server fails to initialize") {
        auto db = std::make_unique<DatabaseManagerPG>(
            "host=127.0.0.1 port=1 user=postgres dbname=none connect_timeout=1");
        REQUIRE(db != nullptr);
        REQUIRE(db->initialize() == false);
    }
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryIPCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("QueryIPCommand executes successfully", "[commands][query][db]") {
    if (!pgAvailable()) {
        SKIP("PostgreSQL 不可达，跳过数据库命令测试（设置 MPING_TEST_PG_CONNSTR 可启用）");
    }
    std::string connstr = createPopulatedDatabase();

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = connstr;
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
}

// ══════════════════════════════════════════════════════════════════════════
//  CleanupCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("CleanupCommand executes successfully", "[commands][cleanup][db]") {
    if (!pgAvailable()) {
        SKIP("PostgreSQL 不可达，跳过数据库命令测试（设置 MPING_TEST_PG_CONNSTR 可启用）");
    }
    std::string connstr = createPopulatedDatabase();

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = connstr;
    cfg.cleanupDays    = 365;  // 清理一年前的数据（应该无影响）

    SECTION("Returns 0") {
        CleanupCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    // 验证数据库仍然可用
    SECTION("Database still usable after cleanup") {
        CleanupCommand cmd(cfg);
        cmd.execute();

        auto db = std::make_unique<DatabaseManagerPG>(connstr);
        REQUIRE(db->initialize());
        auto hosts = db->getAllHosts();
        REQUIRE(hosts.size() >= 2);
    }
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryAlertsCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("QueryAlertsCommand executes successfully", "[commands][alerts][db]") {
    if (!pgAvailable()) {
        SKIP("PostgreSQL 不可达，跳过数据库命令测试（设置 MPING_TEST_PG_CONNSTR 可启用）");
    }
    std::string connstr = createPopulatedDatabase();

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = connstr;

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
}

// ══════════════════════════════════════════════════════════════════════════
//  QueryRecoveryCommand
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("QueryRecoveryCommand executes successfully", "[commands][recovery][db]") {
    if (!pgAvailable()) {
        SKIP("PostgreSQL 不可达，跳过数据库命令测试（设置 MPING_TEST_PG_CONNSTR 可启用）");
    }
    std::string connstr = createPopulatedDatabase();

    // 先解决一个告警，产生恢复记录
    {
        auto db = std::make_unique<DatabaseManagerPG>(connstr);
        REQUIRE(db->initialize());
        db->removeAlert("192.168.1.2");
    }

    ConfigManager::Config cfg;
    cfg.enableDatabase = true;
    cfg.databasePath   = connstr;

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
        // 仓库自带默认 ip.txt，测试须在空临时目录运行以隔离 CWD 依赖；
        // RAII 恢复 CWD，断言失败也保证不影响后续测试
        std::string tmpDir = "/tmp/mping_empty_XXXXXX";
        REQUIRE(mkdtemp(tmpDir.data()) != nullptr);
        struct CwdGuard {
            std::filesystem::path old;
            ~CwdGuard() { std::filesystem::current_path(old); }
        } guard{std::filesystem::current_path()};
        std::filesystem::current_path(tmpDir);

        ConfigManager::Config cfg;
        PingCommand cmd(cfg);
        REQUIRE(cmd.execute() == 1);

        std::filesystem::remove_all(tmpDir);
    }
}

TEST_CASE("PingCommand with file-based hosts", "[commands][ping][file]") {
    // 创建一个符合格式的主机文件
    std::string testFile = "/tmp/test_ping_hosts_XXXXXX.txt";
    int fd               = mkstemps(testFile.data(), 4);
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

    std::filesystem::remove(testFile);
}

TEST_CASE("PingCommand with database enabled", "[commands][ping][db]") {
    if (!pgAvailable()) {
        SKIP("PostgreSQL 不可达，跳过数据库命令测试（设置 MPING_TEST_PG_CONNSTR 可启用）");
    }

    std::string testFile = "/tmp/test_ping_cmd_XXXXXX.txt";
    int fd               = mkstemps(testFile.data(), 4);
    REQUIRE(fd >= 0);
    close(fd);

    std::ofstream file(testFile);
    REQUIRE(file.is_open());
    file << "127.0.0.1 localhost" << std::endl;
    file.close();

    ConfigManager::Config cfg;
    cfg.filename       = testFile;
    cfg.enableDatabase = true;
    cfg.databasePath   = testPgConnstr();
    cfg.silentMode     = true;
    PingCommand cmd(cfg);
    REQUIRE(cmd.execute() == 0);

    // 验证数据库中有数据
    auto db = std::make_unique<DatabaseManagerPG>(testPgConnstr());
    REQUIRE(db->initialize());
    auto hosts = db->getAllHosts();
    REQUIRE(hosts.size() >= 1);

    std::filesystem::remove(testFile);
}

TEST_CASE("PingCommand alert lifecycle", "[commands][ping][alerts][db]") {
    if (!pgAvailable()) {
        SKIP("PostgreSQL 不可达，跳过数据库命令测试（设置 MPING_TEST_PG_CONNSTR 可启用）");
    }

    std::string testFile = "/tmp/test_alert_hosts_XXXXXX.txt";
    int fd               = mkstemps(testFile.data(), 4);
    REQUIRE(fd >= 0);
    close(fd);

    std::ofstream file(testFile);
    REQUIRE(file.is_open());
    file << "127.0.0.1 localhost" << std::endl;
    file.close();

    // 第一次运行：127.0.0.1 可达，不应产生告警
    {
        ConfigManager::Config cfg;
        cfg.filename       = testFile;
        cfg.enableDatabase = true;
        cfg.databasePath   = testPgConnstr();
        cfg.silentMode     = true;
        PingCommand cmd(cfg);
        REQUIRE(cmd.execute() == 0);
    }

    // 验证：没有告警产生
    {
        auto db = std::make_unique<DatabaseManagerPG>(testPgConnstr());
        REQUIRE(db->initialize());
        auto alerts = db->getActiveAlerts();
        // 无 raw ICMP 特权时所有主机视为不可达，会正常产生告警，跳过该断言
        if (haveRawPingCapability()) {
            REQUIRE(alerts.empty());
        }
    }

    std::filesystem::remove(testFile);
}
