#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "utils.h"

TEST_CASE("Utils file reading", "[utils]") {
    SECTION("Read hosts from valid file") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        // Write test data
        std::ofstream file(testFile);
        file << "# ip            hostname\n";
        file << "192.168.1.1    host1\n";
        file << "192.168.1.2    host2\n";
        file.close();

        auto hosts = readHostsFromFile(testFile);
        REQUIRE(hosts.size() == 2);
        REQUIRE(hosts["192.168.1.1"] == "host1");
        REQUIRE(hosts["192.168.1.2"] == "host2");

        close(fd);
        std::filesystem::remove(testFile);
    }

    SECTION("Read hosts from file with comments") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        std::ofstream file(testFile);
        file << "# This is a comment\n";
        file << "192.168.1.1    host1\n";
        file << "# Another comment\n";
        file << "192.168.1.2    host2\n";
        file.close();

        auto hosts = readHostsFromFile(testFile);
        REQUIRE(hosts.size() == 2);

        close(fd);
        std::filesystem::remove(testFile);
    }

    SECTION("Read hosts from empty file") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        auto hosts = readHostsFromFile(testFile);
        REQUIRE(hosts.empty() == true);

        close(fd);
        std::filesystem::remove(testFile);
    }

    SECTION("Read hosts from non-existent file") {
        auto hosts = readHostsFromFile("/non/existent/file.txt");
        REQUIRE(hosts.empty() == true);
    }

    SECTION("Read hosts from file with invalid format") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        std::ofstream file(testFile);
        file << "invalid-line-without-tab\n";  // This will fail to parse (needs two values)
        file.close();

        auto hosts = readHostsFromFile(testFile);
        // The parser requires both IP and hostname, so this line will be skipped
        REQUIRE(hosts.empty() == true);

        close(fd);
        std::filesystem::remove(testFile);
    }

    SECTION("Read hosts with whitespace variations") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        std::ofstream file(testFile);
        file << "192.168.1.1\thost1\n";     // single tab
        file << "192.168.1.2\t\thost2\n";   // double tab
        file << "192.168.1.3\t   host3\n";  // tab with spaces
        file.close();

        auto hosts = readHostsFromFile(testFile);
        REQUIRE(hosts.size() == 3);

        close(fd);
        std::filesystem::remove(testFile);
    }
}

TEST_CASE("Utils edge cases", "[utils]") {
    SECTION("Read hosts with very long hostname") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        std::ofstream file(testFile);
        std::string longHostname(1000, 'a');
        file << "192.168.1.1\t" << longHostname << "\n";
        file.close();

        auto hosts = readHostsFromFile(testFile);
        REQUIRE(hosts.size() == 1);
        REQUIRE(hosts["192.168.1.1"] == longHostname);

        close(fd);
        std::filesystem::remove(testFile);
    }

    SECTION("Read hosts with special characters in hostname") {
        std::string testFile = "/tmp/test_hosts_XXXXXX.txt";
        int fd               = mkstemps(const_cast<char*>(testFile.c_str()), 4);
        REQUIRE(fd >= 0);

        std::ofstream file(testFile);
        file << "192.168.1.1\thost-1_test.server\n";
        file << "192.168.1.2\thost@server\n";
        file.close();

        auto hosts = readHostsFromFile(testFile);
        REQUIRE(hosts.size() == 2);

        close(fd);
        std::filesystem::remove(testFile);
    }
}