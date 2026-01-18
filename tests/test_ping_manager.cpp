#include <catch2/catch_all.hpp>
#include <chrono>
#include <thread>

#include "ping_manager.h"

TEST_CASE("PingManager basic functionality", "[ping]") {
    PingManager pingManager;

    SECTION("Ping single host") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"}};

        auto results = pingManager.performPing(hosts, 1, 1, 1);
        REQUIRE(results.size() == 1);

        const auto& [ip, hostname, success, delay, timestamp] = results[0];
        REQUIRE(ip == "127.0.0.1");
        REQUIRE(hostname == "localhost");
        // localhost should be reachable
        REQUIRE(success == true);
    }

    SECTION("Ping multiple hosts") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"},
                                                    {"127.0.0.2", "localhost2"}};

        auto results = pingManager.performPing(hosts, 1, 1, 2);
        REQUIRE(results.size() == 2);
    }

    SECTION("Ping with invalid host") {
        std::map<std::string, std::string> hosts = {
            {"192.0.2.1", "invalid-host"}  // TEST-NET-1, should be unreachable
        };

        auto results = pingManager.performPing(hosts, 1, 1, 1);
        REQUIRE(results.size() == 1);

        const auto& [ip, hostname, success, delay, timestamp] = results[0];
        REQUIRE(success == false);
        // Failed pings return timeout value (1000ms for 1 second timeout), not -1
        REQUIRE(delay > 0);
    }

    SECTION("Ping with empty hosts list") {
        std::map<std::string, std::string> hosts;

        auto results = pingManager.performPing(hosts, 1, 1, 1);
        REQUIRE(results.empty() == true);
    }

    SECTION("Ping with multiple count") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"}};

        auto results = pingManager.performPing(hosts, 3, 1, 1);
        REQUIRE(results.size() == 1);
    }

    SECTION("Ping with custom timeout") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"}};

        auto results = pingManager.performPing(hosts, 1, 5, 1);
        REQUIRE(results.size() == 1);
    }
}

TEST_CASE("PingManager concurrent execution", "[ping][concurrent]") {
    PingManager pingManager;

    SECTION("Concurrent ping with high concurrency") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"},
                                                    {"127.0.0.2", "localhost2"},
                                                    {"127.0.0.3", "localhost3"}};

        auto results = pingManager.performPing(hosts, 1, 1, 10);
        REQUIRE(results.size() == 3);
    }

    SECTION("Thread safety - multiple concurrent calls") {
        std::map<std::string, std::string> hosts1 = {{"127.0.0.1", "localhost1"}};
        std::map<std::string, std::string> hosts2 = {{"127.0.0.2", "localhost2"}};

        std::vector<std::vector<std::tuple<std::string, std::string, bool, short, std::string>>>
            results1;
        std::vector<std::vector<std::tuple<std::string, std::string, bool, short, std::string>>>
            results2;

        std::thread t1([&]() {
            PingManager pm1;
            results1.push_back(pm1.performPing(hosts1, 1, 1, 1));
        });

        std::thread t2([&]() {
            PingManager pm2;
            results2.push_back(pm2.performPing(hosts2, 1, 1, 1));
        });

        t1.join();
        t2.join();

        REQUIRE(results1.size() == 1);
        REQUIRE(results2.size() == 1);
        REQUIRE(results1[0].size() == 1);
        REQUIRE(results2[0].size() == 1);
    }
}

TEST_CASE("PingManager performance", "[ping][performance]") {
    PingManager pingManager;

    SECTION("Large number of hosts") {
        std::map<std::string, std::string> hosts;
        for (int i = 1; i <= 10; ++i) {
            hosts[std::to_string(127) + "." + std::to_string(0) + "." + std::to_string(0) + "."
                  + std::to_string(i)] = "host" + std::to_string(i);
        }

        auto start   = std::chrono::high_resolution_clock::now();
        auto results = pingManager.performPing(hosts, 1, 1, 10);
        auto end     = std::chrono::high_resolution_clock::now();

        REQUIRE(results.size() == 10);

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        // With 10 concurrent pings, should complete in reasonable time
        REQUIRE(duration.count() < 5000);
    }
}