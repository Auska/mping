#include <catch2/catch_all.hpp>

#include "database_manager.h"

#ifdef USE_POSTGRESQL
#include "database_manager_pg.h"
#endif
#include <filesystem>
#include <fstream>
#include <thread>

#include "database_base.h"
#include "database_factory.h"

TEST_CASE("DatabaseBase IP validation", "[database][base]") {
    SECTION("Valid IPv4 addresses") {
        REQUIRE(DatabaseBaseTest::isValidIP("192.168.1.1") == true);
        REQUIRE(DatabaseBaseTest::isValidIP("10.0.0.1") == true);
        REQUIRE(DatabaseBaseTest::isValidIP("172.16.0.1") == true);
        REQUIRE(DatabaseBaseTest::isValidIP("255.255.255.255") == true);
        REQUIRE(DatabaseBaseTest::isValidIP("0.0.0.0") == true);
    }

    SECTION("Invalid IPv4 addresses") {
        REQUIRE(DatabaseBaseTest::isValidIP("256.1.1.1") == false);
        REQUIRE(DatabaseBaseTest::isValidIP("192.168.1") == false);
        REQUIRE(DatabaseBaseTest::isValidIP("192.168.1.1.1") == false);
        REQUIRE(DatabaseBaseTest::isValidIP("192.168.1.a") == false);
        REQUIRE(DatabaseBaseTest::isValidIP("") == false);
        REQUIRE(DatabaseBaseTest::isValidIP("invalid") == false);
    }
}

TEST_CASE("DatabaseManager initialization", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";

    // Create unique temp file
    int fd = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    SECTION("Successful initialization") {
        DatabaseManager db(testDb);
        REQUIRE(db.initialize() == true);
    }

    SECTION("Failed initialization with invalid path") {
        DatabaseManager db("/invalid/path/to/db.db");
        REQUIRE(db.initialize() == false);
    }

    // Cleanup
    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager ping result insertion", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Insert single ping result") {
        REQUIRE(db.insertPingResult("192.168.1.1", "test-host", 10, true, "2025-01-01 00:00:00")
                == true);
    }

    SECTION("Insert multiple ping results") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results =
            {{"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
             {"192.168.1.2", "host2", 20, true, "2025-01-01 00:00:01"},
             {"10.0.0.1", "host3", 0, false, "2025-01-01 00:00:02"}};
        REQUIRE(db.insertPingResults(results) == true);
    }

    SECTION("Insert with invalid IP") {
        REQUIRE(db.insertPingResult("invalid-ip", "test", 10, true, "2025-01-01 00:00:00")
                == false);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager host management", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Get all hosts from empty database") {
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.empty() == true);
    }

    SECTION("Get hosts after inserting ping results") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results =
            {{"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
             {"192.168.1.2", "host2", 20, true, "2025-01-01 00:00:01"}};
        REQUIRE(db.insertPingResults(results) == true);

        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() == 2);
        REQUIRE(hosts["192.168.1.1"] == "host1");
        REQUIRE(hosts["192.168.1.2"] == "host2");
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager alert management", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Add and remove alert") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);

        auto alerts = db.getActiveAlerts(-1);
        REQUIRE(alerts.size() == 1);

        REQUIRE(db.removeAlert("192.168.1.1") == true);

        alerts = db.getActiveAlerts(-1);
        REQUIRE(alerts.size() == 0);
    }

    SECTION("Get recovery records") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);
        REQUIRE(db.insertPingResult("192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00")
                == true);
        // Remove alert to create recovery record
        REQUIRE(db.removeAlert("192.168.1.1") == true);

        auto records = db.getRecoveryRecords(-1);
        REQUIRE(records.size() == 1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager data cleanup", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Cleanup old data") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
            {"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"}};
        REQUIRE(db.insertPingResults(results) == true);

        // Cleanup data older than 1 day
        db.cleanupOldData(1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager queryIPStatistics", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Query statistics for non-existent IP") {
        REQUIRE_NOTHROW(db.queryIPStatistics("192.168.1.99"));
    }

    SECTION("Query statistics with successful pings") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results =
            {{"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
             {"192.168.1.1", "host1", 15, true, "2025-01-01 00:01:00"},
             {"192.168.1.1", "host1", 20, true, "2025-01-01 00:02:00"}};
        REQUIRE(db.insertPingResults(results) == true);

        REQUIRE_NOTHROW(db.queryIPStatistics("192.168.1.1"));
    }

    SECTION("Query statistics with mixed results") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results =
            {{"192.168.1.2", "host2", 10, true, "2025-01-01 00:00:00"},
             {"192.168.1.2", "host2", 0, false, "2025-01-01 00:01:00"},
             {"192.168.1.2", "host2", 15, true, "2025-01-01 00:02:00"},
             {"192.168.1.2", "host2", 0, false, "2025-01-01 00:03:00"}};
        REQUIRE(db.insertPingResults(results) == true);

        REQUIRE_NOTHROW(db.queryIPStatistics("192.168.1.2"));
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager cleanupOldData", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Cleanup with no data") {
        REQUIRE_NOTHROW(db.cleanupOldData(30));
    }

    SECTION("Cleanup old data") {
        // Insert old data
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results =
            {{"192.168.1.1", "host1", 10, true, "2024-01-01 00:00:00"},
             {"192.168.1.2", "host2", 20, true, "2024-01-01 00:01:00"}};
        REQUIRE(db.insertPingResults(results) == true);

        // Cleanup data older than 1 day
        REQUIRE_NOTHROW(db.cleanupOldData(1));
    }

    SECTION("Cleanup with recent data") {
        // Insert recent data
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
            {"192.168.1.3", "host3", 10, true, "2025-12-26 00:00:00"}};
        REQUIRE(db.insertPingResults(results) == true);

        // Cleanup data older than 30 days (should not delete recent data)
        REQUIRE_NOTHROW(db.cleanupOldData(30));

        // Verify host still exists
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() == 1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager getActiveAlerts", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Get alerts from empty database") {
        auto alerts = db.getActiveAlerts(-1);
        REQUIRE(alerts.empty() == true);
    }

    SECTION("Get all alerts") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);
        REQUIRE(db.addAlert("192.168.1.2", "host2") == true);

        auto alerts = db.getActiveAlerts(-1);
        REQUIRE(alerts.size() == 2);
    }

    SECTION("Get alerts with days filter") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);

        auto allAlerts = db.getActiveAlerts(-1);
        REQUIRE(allAlerts.size() == 1);

        auto recentAlerts = db.getActiveAlerts(7);
        REQUIRE(recentAlerts.size() == 1);
    }

    SECTION("Get alerts with old filter") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);

        // Query alerts from 1 day ago (should return the new alert)
        auto recentAlerts = db.getActiveAlerts(1);
        REQUIRE(recentAlerts.size() == 1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager getRecoveryRecords", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Get recovery records from empty database") {
        auto records = db.getRecoveryRecords(-1);
        REQUIRE(records.empty() == true);
    }

    SECTION("Get all recovery records") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);
        REQUIRE(db.removeAlert("192.168.1.1") == true);

        REQUIRE(db.addAlert("192.168.1.2", "host2") == true);
        REQUIRE(db.removeAlert("192.168.1.2") == true);

        auto records = db.getRecoveryRecords(-1);
        REQUIRE(records.size() == 2);
    }

    SECTION("Get recovery records with days filter") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);
        REQUIRE(db.removeAlert("192.168.1.1") == true);

        auto allRecords = db.getRecoveryRecords(-1);
        REQUIRE(allRecords.size() == 1);

        auto recentRecords = db.getRecoveryRecords(7);
        REQUIRE(recentRecords.size() == 1);
    }

    SECTION("Recovery record contains correct data") {
        REQUIRE(db.addAlert("192.168.1.1", "test-host") == true);
        REQUIRE(db.removeAlert("192.168.1.1") == true);

        auto records = db.getRecoveryRecords(-1);
        REQUIRE(records.size() == 1);

        const auto& [id, ip, hostname, alertTime, recoveryTime] = records[0];
        REQUIRE(ip == "192.168.1.1");
        REQUIRE(hostname == "test-host");
        REQUIRE(alertTime.empty() == false);
        REQUIRE(recoveryTime.empty() == false);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager alert management edge cases", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Add alert with invalid IP") {
        REQUIRE(db.addAlert("invalid-ip", "host1") == false);
    }

    SECTION("Remove alert with invalid IP") {
        REQUIRE(db.removeAlert("invalid-ip") == false);
    }

    SECTION("Remove non-existent alert") {
        REQUIRE(db.removeAlert("192.168.1.99") == true);  // Should succeed even if not exists
    }

    SECTION("Add duplicate alert") {
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);
        REQUIRE(db.addAlert("192.168.1.1", "host1")
                == true);  // Should succeed (ON CONFLICT DO NOTHING)

        auto alerts = db.getActiveAlerts(-1);
        REQUIRE(alerts.size() == 1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager cleanupOldData clears all tables", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Cleanup clears ping_results with old timestamps") {
        // Insert ping result with old timestamp
        REQUIRE(db.insertPingResult("192.168.1.1", "host1", 10, true, "2024-01-01 00:00:00")
                == true);

        // Cleanup with 1 day should clear old data
        REQUIRE_NOTHROW(db.cleanupOldData(1));

        // Verify hosts still exists (hosts should NOT be cleaned)
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() == 1);
        REQUIRE(hosts["192.168.1.1"] == "host1");
    }

    SECTION("Cleanup preserves hosts table") {
        // Insert multiple hosts with old timestamps
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results =
            {{"192.168.1.1", "host1", 10, true, "2024-01-01 00:00:00"},
             {"192.168.1.2", "host2", 20, true, "2024-01-01 00:00:01"},
             {"192.168.1.3", "host3", 30, true, "2024-01-01 00:00:02"}};
        REQUIRE(db.insertPingResults(results) == true);

        auto hostsBefore = db.getAllHosts();
        REQUIRE(hostsBefore.size() == 3);

        // Cleanup with 1 day
        REQUIRE_NOTHROW(db.cleanupOldData(1));

        // Verify hosts are preserved
        auto hostsAfter = db.getAllHosts();
        REQUIRE(hostsAfter.size() == 3);
        REQUIRE(hostsAfter["192.168.1.1"] == "host1");
        REQUIRE(hostsAfter["192.168.1.2"] == "host2");
        REQUIRE(hostsAfter["192.168.1.3"] == "host3");
    }

    SECTION("Cleanup with alerts and recovery records does not fail") {
        // Insert ping result first to create host entry
        REQUIRE(db.insertPingResult("192.168.1.1", "host1", 10, true, "2024-01-01 00:00:00")
                == true);

        // Add alert and create recovery record
        REQUIRE(db.addAlert("192.168.1.1", "host1") == true);
        REQUIRE(db.removeAlert("192.168.1.1") == true);

        // Cleanup should not fail even with alerts and recovery records
        REQUIRE_NOTHROW(db.cleanupOldData(30));

        // Verify hosts table is preserved
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() == 1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager batch operations", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Insert large batch of results") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results;
        for (int i = 0; i < 100; ++i) {
            results.emplace_back("192.168.1." + std::to_string(i % 255), "host" + std::to_string(i),
                                 i % 100, i % 2 == 0, "2025-01-01 00:00:00");
        }
        REQUIRE(db.insertPingResults(results) == true);

        auto hosts = db.getAllHosts();
        REQUIRE(hosts.size() > 0);
    }

    SECTION("Insert empty batch") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results;
        REQUIRE(db.insertPingResults(results) == true);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseFactory", "[database][factory]") {
    SECTION("Create SQLite database") {
        auto db = DatabaseFactory::createDatabase(DatabaseType::SQLITE, "/tmp/test_factory.db");
        REQUIRE(db != nullptr);
        REQUIRE(db->initialize() == true);

        std::filesystem::remove("/tmp/test_factory.db");
    }

    SECTION("Detect database type from connection string") {
        REQUIRE(DatabaseFactory::detectDatabaseType("test.db") == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("/path/to/test.db") == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("./relative/path.db") == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("../parent/path.db") == DatabaseType::SQLITE);
#ifdef USE_POSTGRESQL
        REQUIRE(DatabaseFactory::detectDatabaseType("host=localhost user=test")
                == DatabaseType::POSTGRESQL);
        REQUIRE(DatabaseFactory::detectDatabaseType("port=5432 dbname=test")
                == DatabaseType::POSTGRESQL);
        REQUIRE(DatabaseFactory::detectDatabaseType("user=test password=secret")
                == DatabaseType::POSTGRESQL);
        REQUIRE(DatabaseFactory::detectDatabaseType("password=secret dbname=mydb")
                == DatabaseType::POSTGRESQL);
#endif
    }

    SECTION("Detect database type with various PostgreSQL patterns") {
#ifdef USE_POSTGRESQL
        REQUIRE(DatabaseFactory::detectDatabaseType(
                    "host=localhost port=5432 user=test password=secret dbname=mydb")
                == DatabaseType::POSTGRESQL);
        REQUIRE(DatabaseFactory::detectDatabaseType("dbname=test host=example.com")
                == DatabaseType::POSTGRESQL);
        REQUIRE(DatabaseFactory::detectDatabaseType(
                    "user=postgres password=test123 host=127.0.0.1 port=5433 dbname=monitor")
                == DatabaseType::POSTGRESQL);
#else
        // When PostgreSQL is disabled, all connection strings should return SQLite
        REQUIRE(DatabaseFactory::detectDatabaseType("host=localhost user=test")
                == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("port=5432 dbname=test")
                == DatabaseType::SQLITE);
#endif
    }

    SECTION("Detect database type with SQLite paths containing keywords") {
        REQUIRE(DatabaseFactory::detectDatabaseType("/path/to/host.db") == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("/path/to/user.db") == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("/path/to/password.db")
                == DatabaseType::SQLITE);
        REQUIRE(DatabaseFactory::detectDatabaseType("/path/to/port.db") == DatabaseType::SQLITE);
    }
}
// ══════════════════════════════════════════════════════════════════════════
//  告警 → 恢复记录 完整生命周期
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("DatabaseManager alert lifecycle integration", "[database][sqlite][integration]") {
    std::string testDb = "/tmp/test_lifecycle_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize());

    SECTION("Full lifecycle: add, verify, remove, verify recovery") {
        REQUIRE(db.addAlert("10.0.0.1", "router1"));
        auto alerts1 = db.getActiveAlerts();
        REQUIRE(alerts1.size() == 1);

        REQUIRE(db.removeAlert("10.0.0.1"));
        REQUIRE(db.getActiveAlerts().empty());

        auto records = db.getRecoveryRecords();
        REQUIRE(records.size() == 1);
        const auto& [id, ip, hostname, alertTime, recoveryTime] = records[0];
        REQUIRE(ip == "10.0.0.1");
        REQUIRE(hostname == "router1");
        REQUIRE(alertTime.empty() == false);
        REQUIRE(recoveryTime.empty() == false);
    }

    SECTION("Multiple alerts with mixed resolutions") {
        REQUIRE(db.addAlert("10.0.0.1", "host1"));
        REQUIRE(db.addAlert("10.0.0.2", "host2"));
        REQUIRE(db.getActiveAlerts().size() == 2);
        db.removeAlert("10.0.0.1");
        REQUIRE(db.getActiveAlerts().size() == 1);
        REQUIRE(db.getRecoveryRecords().size() == 1);
    }

    SECTION("Removing non-existent alert does not create recovery record") {
        db.removeAlert("10.0.0.99");
        REQUIRE(db.getRecoveryRecords().empty());
    }

    std::filesystem::remove(testDb);
}

// ══════════════════════════════════════════════════════════════════════════
//  cleanupOldPingResults
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("DatabaseManager cleanupOldPingResults", "[database][sqlite]") {
    std::string testDb = "/tmp/test_cleanuppr_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize());

    SECTION("Cleanup with no data") {
        REQUIRE_NOTHROW(db.cleanupOldPingResults(30));
    }

    SECTION("Cleanup old ping results keeps hosts and alerts") {
        db.insertPingResult("192.168.1.1", "host1", 10, true, "2024-01-01 00:00:00");
        db.addAlert("10.0.0.1", "router1");
        db.cleanupOldPingResults(1);
        REQUIRE(db.getAllHosts().size() == 1);
        REQUIRE(db.getActiveAlerts().size() == 1);
    }

    std::filesystem::remove(testDb);
}

// ══════════════════════════════════════════════════════════════════════════
//  并发数据库访问
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("DatabaseManager concurrent access", "[database][sqlite][concurrent]") {
    std::string testDb = "/tmp/test_concurrent_XXXXXX.db";
    int fd             = mkstemps(testDb.data(), 3);
    REQUIRE(fd >= 0);
    close(fd);

    SECTION("Concurrent insertions from multiple threads") {
        DatabaseManager db(testDb);
        REQUIRE(db.initialize());

        std::vector<std::thread> threads;
        constexpr int THREAD_COUNT       = 4;
        constexpr int INSERTS_PER_THREAD = 100;

        for (int t = 0; t < THREAD_COUNT; ++t) {
            threads.emplace_back([&db, t]() {
                for (int i = 0; i < INSERTS_PER_THREAD; ++i) {
                    std::string ip = "10.0." + std::to_string(t) + "." + std::to_string(i);
                    db.insertPingResult(ip, "host", i, true, "2025-01-01 00:00:00");
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        REQUIRE(db.getAllHosts().size() >= THREAD_COUNT * INSERTS_PER_THREAD * 3 / 4);
    }

    SECTION("Concurrent alert operations") {
        DatabaseManager db(testDb);
        REQUIRE(db.initialize());

        constexpr int THREAD_COUNT = 4;
        std::vector<std::thread> threads;

        for (int t = 0; t < THREAD_COUNT; ++t) {
            threads.emplace_back([&db, t]() {
                std::string ip = "192.168." + std::to_string(t) + "." + std::to_string(t);
                db.addAlert(ip, "host");
                db.getActiveAlerts();
                db.removeAlert(ip);
                db.getRecoveryRecords();
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        REQUIRE(db.getActiveAlerts().empty());
        REQUIRE(db.getRecoveryRecords().size() == THREAD_COUNT);
    }

    std::filesystem::remove(testDb);
}
