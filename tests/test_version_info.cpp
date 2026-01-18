#include <catch2/catch_all.hpp>
#include <iostream>
#include <regex>
#include <sstream>

#include "version_info.h"

// Helper function to capture stdout
std::string capture_output(std::function<void()> func) {
    std::ostringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
    func();
    std::cout.rdbuf(old);
    return buffer.str();
}

TEST_CASE("VersionInfo output format", "[version]") {
    SECTION("Version info contains project name") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("mping") != std::string::npos);
    }

    SECTION("Version info contains version number") {
        std::string output = capture_output([]() { print_version_info(); });
        // Check for version pattern (e.g., "1.1.0")
        REQUIRE(std::regex_search(output, std::regex("\\d+\\.\\d+\\.\\d+")));
    }

    SECTION("Version info contains description") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("Multi-host Ping Tool") != std::string::npos);
    }

    SECTION("Version info contains homepage") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("https://github.com/Auska/mping") != std::string::npos);
    }

    SECTION("Version info contains build time") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("Build Time") != std::string::npos);
    }

    SECTION("Version info contains compiler information") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("Compiler") != std::string::npos);
    }

    SECTION("Version info contains platform information") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("Platform") != std::string::npos);
    }
}

TEST_CASE("VersionInfo version components", "[version]") {
    SECTION("Major version is present") {
        std::string output = capture_output([]() { print_version_info(); });
        REQUIRE(output.find("Version:") != std::string::npos);
    }

    SECTION("Version format is correct") {
        std::string output = capture_output([]() { print_version_info(); });
        // Check for "Version: X.Y.Z" pattern
        REQUIRE(std::regex_search(output, std::regex("Version:\\s*\\d+\\.\\d+\\.\\d+")));
    }
}

TEST_CASE("VersionInfo compiler detection", "[version]") {
    std::string output = capture_output([]() { print_version_info(); });

    SECTION("Detects GCC compiler") {
#ifdef __GNUC__
        REQUIRE(output.find("GCC") != std::string::npos);
#endif
    }

    SECTION("Detects Clang compiler") {
#ifdef __clang__
        REQUIRE(output.find("Clang") != std::string::npos);
#endif
    }

    SECTION("Detects MSVC compiler") {
#ifdef _MSC_VER
        REQUIRE(output.find("MSVC") != std::string::npos);
#endif
    }
}

TEST_CASE("VersionInfo platform detection", "[version]") {
    std::string output = capture_output([]() { print_version_info(); });

    SECTION("Detects Linux platform") {
#ifdef __linux__
        REQUIRE(output.find("Linux") != std::string::npos);
#endif
    }

    SECTION("Detects macOS platform") {
#ifdef __APPLE__
        REQUIRE(output.find("macOS") != std::string::npos);
#endif
    }

    SECTION("Detects Windows platform") {
#if defined(_WIN32) || defined(_WIN64)
        REQUIRE(output.find("Windows") != std::string::npos);
#endif
    }

    SECTION("Detects FreeBSD platform") {
#ifdef __FreeBSD__
        REQUIRE(output.find("FreeBSD") != std::string::npos);
#endif
    }
}

TEST_CASE("VersionInfo no crashes", "[version]") {
    SECTION("Function executes without crashing") {
        REQUIRE_NOTHROW([]() { print_version_info(); }());
    }

    SECTION("Function can be called multiple times") {
        REQUIRE_NOTHROW([]() {
            print_version_info();
            print_version_info();
            print_version_info();
        }());
    }
}
