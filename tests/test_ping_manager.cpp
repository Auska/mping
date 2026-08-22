#include <catch2/catch_all.hpp>
#include <chrono>
#include <future>
#include <thread>

#include "commands.h"
#include "ping_manager.h"
#include "test_helpers.h"

namespace {
constexpr int kWindow = ConfigDefaults::DOWN_CONFIRM_WINDOW;
}

TEST_CASE("PingManager basic functionality", "[ping]") {
    PingManager pingManager;

    SECTION("Ping single host") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"}};

        auto results = pingManager.checkHosts(hosts, kWindow, 1);
        REQUIRE(results.size() == 1);

        const auto& result = results[0];
        REQUIRE(result.ip == "127.0.0.1");
        REQUIRE(result.hostname == "localhost");
        // localhost 可达性依赖 raw ICMP 特权，无特权时跳过
        if (haveRawPingCapability()) {
            REQUIRE(result.success == true);
        }
    }

    SECTION("Ping multiple hosts") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"},
                                                    {"127.0.0.2", "localhost2"}};

        auto results = pingManager.checkHosts(hosts, kWindow, 2);
        REQUIRE(results.size() == 2);
    }

    SECTION("Ping with invalid host") {
        std::map<std::string, std::string> hosts = {
            {"192.0.2.1", "invalid-host"}  // TEST-NET-1, should be unreachable
        };

        auto results = pingManager.checkHosts(hosts, kWindow, 1);
        REQUIRE(results.size() == 1);

        const auto& result = results[0];
        REQUIRE(result.success == false);
        // Failed pings return timeout value (1000ms for 1 second timeout), not -1
        REQUIRE(result.delayMs > 0);
    }

    SECTION("Invalid IP produces a failure result, never deadlocks") {
        // 空地址使 IPAddress 构造必然抛异常；pingHost 内部捕获并按不可达处理。
        // 若任务完成计数不递减（或窗口循环不收敛），这里会永久挂起
        std::map<std::string, std::string> hosts = {{"", "bad-host"}};
        auto future                              = std::async(std::launch::async,
                                                              [&] { return pingManager.checkHosts(hosts, kWindow, 1).size(); });
        REQUIRE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        REQUIRE(future.get() == 1);  // 窗口收敛后输出失败结果而非挂起
    }

    SECTION("Ping with empty hosts list") {
        std::map<std::string, std::string> hosts;

        auto results = pingManager.checkHosts(hosts, kWindow, 1);
        REQUIRE(results.empty() == true);
    }
}

TEST_CASE("PingManager concurrent execution", "[ping][concurrent]") {
    PingManager pingManager;

    SECTION("Concurrent ping with high concurrency") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"},
                                                    {"127.0.0.2", "localhost2"},
                                                    {"127.0.0.3", "localhost3"}};

        auto results = pingManager.checkHosts(hosts, kWindow, 10);
        REQUIRE(results.size() == 3);
    }

    SECTION("Thread safety - multiple concurrent calls") {
        std::map<std::string, std::string> hosts1 = {{"127.0.0.1", "localhost1"}};
        std::map<std::string, std::string> hosts2 = {{"127.0.0.2", "localhost2"}};

        std::vector<std::vector<PingResult>> results1;
        std::vector<std::vector<PingResult>> results2;

        std::thread t1([&]() {
            PingManager pm1;
            results1.push_back(pm1.checkHosts(hosts1, kWindow, 1));
        });

        std::thread t2([&]() {
            PingManager pm2;
            results2.push_back(pm2.checkHosts(hosts2, kWindow, 1));
        });

        t1.join();
        t2.join();

        REQUIRE(results1.size() == 1);
        REQUIRE(results2.size() == 1);
        REQUIRE(results1[0].size() == 1);
        REQUIRE(results2[0].size() == 1);
    }
}

TEST_CASE("PingManager sliding-window check", "[ping][window]") {
    PingManager pingManager;

    SECTION("Empty hosts list returns empty") {
        std::map<std::string, std::string> hosts;
        auto results = pingManager.checkHosts(hosts, kWindow, 1);
        REQUIRE(results.empty());
    }

    SECTION("Window size 1 gives immediate verdict (fast path)") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"}};
        auto results                             = pingManager.checkHosts(hosts, 1, 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].ip == "127.0.0.1");
        if (haveRawPingCapability()) {
            REQUIRE(results[0].success);
        }
    }

    SECTION("Reachable host judged up in the first round") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"}};
        auto results                             = pingManager.checkHosts(hosts, kWindow, 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].ip == "127.0.0.1");
        if (haveRawPingCapability()) {
            REQUIRE(results[0].success);
        }
    }

    SECTION("Unreachable host judged down after the full window") {
        std::map<std::string, std::string> hosts = {
            {"192.0.2.1", "invalid-host"}  // TEST-NET-1, should be unreachable
        };
        auto results = pingManager.checkHosts(hosts, kWindow, 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].success == false);
    }

    SECTION("Mixed hosts: reachable recovers, unreachable stays down") {
        std::map<std::string, std::string> hosts = {{"127.0.0.1", "localhost"},
                                                    {"192.0.2.1", "invalid-host"}};
        auto results                             = pingManager.checkHosts(hosts, kWindow, 2);
        REQUIRE(results.size() == 2);
        bool reachableSeen   = false;
        bool unreachableSeen = false;
        for (const auto& r : results) {
            if (r.ip == "127.0.0.1") {
                if (haveRawPingCapability()) {
                    REQUIRE(r.success);
                }
                reachableSeen = true;
            } else if (r.ip == "192.0.2.1") {
                REQUIRE(r.success == false);
                unreachableSeen = true;
            }
        }
        REQUIRE(reachableSeen);
        REQUIRE(unreachableSeen);
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
        auto results = pingManager.checkHosts(hosts, kWindow, 10);
        auto end     = std::chrono::high_resolution_clock::now();

        REQUIRE(results.size() == 10);

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        // With 10 concurrent pings, should complete in reasonable time
        REQUIRE(duration.count() < 5000);
    }
}
