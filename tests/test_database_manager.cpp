#include <catch2/catch_all.hpp>
#include "database_manager.h"

#ifdef USE_POSTGRESQL
#include "database_manager_pg.h"
#endif
#include "database_factory.h"
#include "database_base.h"
#include <filesystem>
#include <fstream>

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
    int fd = mkstemps(const_cast<char*>(testDb.c_str()), 3);
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
    int fd = mkstemps(const_cast<char*>(testDb.c_str()), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Insert single ping result") {
        REQUIRE(db.insertPingResult("192.168.1.1", "test-host", 10, true, "2025-01-01 00:00:00") == true);
    }

    SECTION("Insert multiple ping results") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
            {"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
            {"192.168.1.2", "host2", 20, true, "2025-01-01 00:00:01"},
            {"10.0.0.1", "host3", 0, false, "2025-01-01 00:00:02"}
        };
        REQUIRE(db.insertPingResults(results) == true);
    }

    SECTION("Insert with invalid IP") {
        REQUIRE(db.insertPingResult("invalid-ip", "test", 10, true, "2025-01-01 00:00:00") == false);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager host management", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd = mkstemps(const_cast<char*>(testDb.c_str()), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Get all hosts from empty database") {
        auto hosts = db.getAllHosts();
        REQUIRE(hosts.empty() == true);
    }

    SECTION("Get hosts after inserting ping results") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
            {"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"},
            {"192.168.1.2", "host2", 20, true, "2025-01-01 00:00:01"}
        };
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
    int fd = mkstemps(const_cast<char*>(testDb.c_str()), 3);
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
        REQUIRE(db.insertPingResult("192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00") == true);
        // Remove alert to create recovery record
        REQUIRE(db.removeAlert("192.168.1.1") == true);
        
        auto records = db.getRecoveryRecords(-1);
        REQUIRE(records.size() == 1);
    }

    std::filesystem::remove(testDb);
}

TEST_CASE("DatabaseManager data cleanup", "[database][sqlite]") {
    std::string testDb = "/tmp/test_mping_XXXXXX.db";
    int fd = mkstemps(const_cast<char*>(testDb.c_str()), 3);
    REQUIRE(fd >= 0);
    close(fd);

    DatabaseManager db(testDb);
    REQUIRE(db.initialize() == true);

    SECTION("Cleanup old data") {
        std::vector<std::tuple<std::string, std::string, short, bool, std::string>> results = {
            {"192.168.1.1", "host1", 10, true, "2025-01-01 00:00:00"}
        };
        REQUIRE(db.insertPingResults(results) == true);
        
        // Cleanup data older than 1 day
        db.cleanupOldData(1);
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
#ifdef USE_POSTGRESQL
        REQUIRE(DatabaseFactory::detectDatabaseType("host=localhost user=test") == DatabaseType::POSTGRESQL);
#endif
    }
}