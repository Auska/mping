#include <unistd.h>

#include <catch2/catch_all.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "config_manager.h"

// Helper function to reset getopt global variables
void reset_getopt() {
#ifdef __GLIBC__
    optind = 0;  // glibc: 0 triggers full re-initialization
#else
    optind = 1;  // musl/macOS: 1 is sufficient after the first call
#endif
    opterr = 0;  // suppress error messages during testing
    optopt = '?';
    optarg = nullptr;
}

// Helper: build char* argv from string literals for parseArguments testing
struct Argv {
    std::vector<std::string> storage;
    std::vector<char*> argv;

    Argv(std::initializer_list<const char*> args) {
        // 先完整填充再取指针：emplace_back 触发 vector 扩容会把已有 string 移动走，
        // 对 SSO 短字符串，移动后先前 data() 指针悬空（ASan heap-use-after-free）
        storage.reserve(args.size());
        for (const char* a : args) {
            storage.emplace_back(a);
        }
        for (auto& s : storage) {
            argv.push_back(s.data());
        }
    }

    int size() const { return static_cast<int>(argv.size()); }
    char** data() { return argv.data(); }
};

TEST_CASE("ConfigManager default values", "[config]") {
    SECTION("Default configuration values") {
        ConfigManager configManager(false);  // 禁用配置文件加载
        const auto& config = configManager.getConfig();

        REQUIRE(config.filename == "");
        REQUIRE(config.enableDatabase == false);
        REQUIRE(config.databasePath == "host=localhost user=postgres dbname=mping_pgtest");
        REQUIRE(config.silentMode == false);
        REQUIRE(config.queryIP == "");
        REQUIRE(config.cleanupDays == -1);
        REQUIRE(config.queryAlerts == -1);
        REQUIRE(config.queryRecoveryRecords == -1);
    }
}

TEST_CASE("ConfigManager help option", "[config]") {
    SECTION("-h option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-h"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }

    SECTION("--help option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--help"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }
}

TEST_CASE("ConfigManager version option", "[config]") {
    SECTION("-v option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-v"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }

    SECTION("--version option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--version"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }
}

TEST_CASE("ConfigManager database option", "[config]") {
    SECTION("-d option with path") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-d", "/path/to/db.db"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "/path/to/db.db");
    }

    SECTION("--database option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--database", "test.db"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "test.db");
    }
}

TEST_CASE("ConfigManager file option", "[config]") {
    SECTION("-f option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-f", "hosts.txt"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "hosts.txt");
    }

    SECTION("--file option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--file", "my_ips.txt"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "my_ips.txt");
    }
}

TEST_CASE("ConfigManager query option", "[config]") {
    SECTION("-q option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-q", "192.168.1.1"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryIP == "192.168.1.1");
    }

    SECTION("--query option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--query", "10.0.0.1"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryIP == "10.0.0.1");
    }
}

TEST_CASE("ConfigManager alerts option", "[config]") {
    SECTION("-a option without days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-a"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == -2);
    }

    SECTION("-a option with days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-a7"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == 7);
    }

    SECTION("-a option with space-separated days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-a", "7"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == 7);
        REQUIRE(config.filename == "");
    }

    SECTION("-a with days and positional file") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-a", "7", "extra.txt"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == 7);
        REQUIRE(config.filename == "extra.txt");
    }

    SECTION("--alerts option with days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--alerts=30"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == 30);
    }

    SECTION("-a option with invalid days (negative)") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-a-5"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }

    SECTION("-a option with invalid days (non-numeric)") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-aabc"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }
}

TEST_CASE("ConfigManager recovery option", "[config]") {
    SECTION("-r option without days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-r"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == -2);
    }

    SECTION("-r option with days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-r14"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == 14);
    }

    SECTION("-r option with space-separated days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-r", "14"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == 14);
        REQUIRE(config.filename == "");
    }

    SECTION("--recovery option with days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--recovery=60"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == 60);
    }

    SECTION("-r option with invalid days (negative)") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-r-10"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }

    SECTION("-r option with invalid days (non-numeric)") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-rxyz"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }
}

TEST_CASE("ConfigManager silent option", "[config]") {
    SECTION("-s option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-s"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.silentMode == true);
    }

    SECTION("--silent option") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--silent"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.silentMode == true);
    }
}

TEST_CASE("ConfigManager cleanup option", "[config]") {
    SECTION("-C option without days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-C"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 30);
    }

    SECTION("-C option with days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-C90"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 90);
    }

    SECTION("-C option with space-separated days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-C", "90"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 90);
        REQUIRE(config.filename == "");
    }

    SECTION("--cleanup option with space-separated days") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--cleanup", "15"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 15);
        REQUIRE(config.filename == "");
    }

    SECTION("-C with non-numeric positional keeps filename") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-C", "my_hosts.txt"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 30);  // 默认 30 天
        REQUIRE(config.filename == "my_hosts.txt");
    }
}

TEST_CASE("ConfigManager positional argument", "[config]") {
    SECTION("Positional filename argument") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "my_hosts.txt"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "my_hosts.txt");
    }

    SECTION("Options override positional argument") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-f", "option_file.txt", "positional_file.txt"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "positional_file.txt");
    }
}

TEST_CASE("ConfigManager multiple options", "[config]") {
    SECTION("Multiple valid options") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-d", "test.db", "-s"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "test.db");
        REQUIRE(config.silentMode == true);
    }

    SECTION("All options combined") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-d", "monitor.db", "-f", "hosts.txt", "-s"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "monitor.db");
        REQUIRE(config.filename == "hosts.txt");
        REQUIRE(config.silentMode == true);
    }
}

TEST_CASE("ConfigManager invalid arguments", "[config]") {
    SECTION("Unknown option is rejected") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-x"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }

    SECTION("Unknown long option is rejected") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "--bogus"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }

    SECTION("Missing required argument is rejected") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping", "-d"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }
}

TEST_CASE("ConfigManager no arguments", "[config]") {
    SECTION("No arguments shows help") {
        reset_getopt();
        ConfigManager configManager;
        auto argv = Argv({"mping"});
        REQUIRE(configManager.parseArguments(argv.size(), argv.data()) == false);
    }
}