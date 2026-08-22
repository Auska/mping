#include <catch2/catch_all.hpp>
#include <memory>
#include <stdexcept>

#include "database_manager_pg.h"
#include "ip_validator.h"

TEST_CASE("IPv4 validation", "[validation]") {
    SECTION("Valid IPv4 addresses") {
        REQUIRE(IPValidator::isValidIPv4("192.168.1.1") == true);
        REQUIRE(IPValidator::isValidIPv4("10.0.0.1") == true);
        REQUIRE(IPValidator::isValidIPv4("172.16.0.1") == true);
        REQUIRE(IPValidator::isValidIPv4("255.255.255.255") == true);
        REQUIRE(IPValidator::isValidIPv4("0.0.0.0") == true);
    }

    SECTION("Invalid IPv4 addresses") {
        REQUIRE(IPValidator::isValidIPv4("256.1.1.1") == false);
        REQUIRE(IPValidator::isValidIPv4("192.168.1") == false);
        REQUIRE(IPValidator::isValidIPv4("192.168.1.1.1") == false);
        REQUIRE(IPValidator::isValidIPv4("192.168.1.a") == false);
        REQUIRE(IPValidator::isValidIPv4("") == false);
        REQUIRE(IPValidator::isValidIPv4("invalid") == false);
    }
}

TEST_CASE("DatabaseManagerPG construction", "[database]") {
    SECTION("Empty connection string throws") {
        REQUIRE_THROWS_AS(std::make_unique<DatabaseManagerPG>(""), std::invalid_argument);
    }

    SECTION("Non-empty connection string constructs") {
        auto db =
            std::make_unique<DatabaseManagerPG>("host=localhost user=postgres dbname=postgres");
        REQUIRE(db != nullptr);
    }

    SECTION("Unreachable server fails to initialize") {
        auto db = std::make_unique<DatabaseManagerPG>(
            "host=127.0.0.1 port=1 user=postgres dbname=none connect_timeout=1");
        REQUIRE(db != nullptr);
        REQUIRE(db->initialize() == false);
    }
}
