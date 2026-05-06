#include <unistd.h>

#include <catch2/catch_all.hpp>
#include <cstring>

#include "config_manager.h"

// Helper function to reset getopt global variables
void reset_getopt() {
    optind = 1;
    opterr = 1;
    optopt = '?';
    optarg = nullptr;
}

TEST_CASE("ConfigManager default values", "[config]") {
    SECTION("Default configuration values") {
        ConfigManager configManager(false);  // 禁用配置文件加载
        const auto& config = configManager.getConfig();

        REQUIRE(config.filename == "");
        REQUIRE(config.enableDatabase == false);
        REQUIRE(config.databasePath == "ping_monitor.db");
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
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-h")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }

    SECTION("--help option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--help")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }
}

TEST_CASE("ConfigManager version option", "[config]") {
    SECTION("-v option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-v")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }

    SECTION("--version option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--version")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }
}

TEST_CASE("ConfigManager database option", "[config]") {
    SECTION("-d option with path") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-d"),
                        const_cast<char*>("/path/to/db.db")};
        REQUIRE(configManager.parseArguments(3, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "/path/to/db.db");
    }

    SECTION("--database option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--database"),
                        const_cast<char*>("test.db")};
        REQUIRE(configManager.parseArguments(3, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "test.db");
    }
}

TEST_CASE("ConfigManager file option", "[config]") {
    SECTION("-f option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-f"),
                        const_cast<char*>("hosts.txt")};
        REQUIRE(configManager.parseArguments(3, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "hosts.txt");
    }

    SECTION("--file option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--file"),
                        const_cast<char*>("my_ips.txt")};
        REQUIRE(configManager.parseArguments(3, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "my_ips.txt");
    }
}

TEST_CASE("ConfigManager query option", "[config]") {
    SECTION("-q option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-q"),
                        const_cast<char*>("192.168.1.1")};
        REQUIRE(configManager.parseArguments(3, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryIP == "192.168.1.1");
    }

    SECTION("--query option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--query"),
                        const_cast<char*>("10.0.0.1")};
        REQUIRE(configManager.parseArguments(3, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryIP == "10.0.0.1");
    }
}

TEST_CASE("ConfigManager alerts option", "[config]") {
    SECTION("-a option without days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-a")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == -2);
    }

    SECTION("-a option with days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-a7")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == 7);
    }

    SECTION("--alerts option with days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--alerts=30")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryAlerts == 30);
    }

    SECTION("-a option with invalid days (negative)") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-a-5")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }

    SECTION("-a option with invalid days (non-numeric)") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-aabc")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }
}

TEST_CASE("ConfigManager recovery option", "[config]") {
    SECTION("-r option without days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-r")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == -2);
    }

    SECTION("-r option with days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-r14")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == 14);
    }

    SECTION("--recovery option with days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--recovery=60")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.queryRecoveryRecords == 60);
    }

    SECTION("-r option with invalid days (negative)") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-r-10")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }

    SECTION("-r option with invalid days (non-numeric)") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-rxyz")};
        REQUIRE(configManager.parseArguments(2, argv) == false);
    }
}

TEST_CASE("ConfigManager silent option", "[config]") {
    SECTION("-s option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-s")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.silentMode == true);
    }

    SECTION("--silent option") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--silent")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.silentMode == true);
    }
}

TEST_CASE("ConfigManager cleanup option", "[config]") {
    SECTION("-C option without days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-C")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 30);
    }

    SECTION("-C option with days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-C90")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 90);
    }

    SECTION("--cleanup option with days") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("--cleanup=15")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.cleanupDays == 15);
    }
}

TEST_CASE("ConfigManager positional argument", "[config]") {
    SECTION("Positional filename argument") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("my_hosts.txt")};
        REQUIRE(configManager.parseArguments(2, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "my_hosts.txt");
    }

    SECTION("Options override positional argument") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"), const_cast<char*>("-f"),
                        const_cast<char*>("option_file.txt"),
                        const_cast<char*>("positional_file.txt")};
        REQUIRE(configManager.parseArguments(4, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.filename == "positional_file.txt");
    }
}

TEST_CASE("ConfigManager multiple options", "[config]") {
    SECTION("Multiple valid options") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"),   const_cast<char*>("-d"),
                        const_cast<char*>("test.db"), const_cast<char*>("-s")};
        REQUIRE(configManager.parseArguments(4, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "test.db");
        REQUIRE(config.silentMode == true);
    }

    SECTION("All options combined") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping"),      const_cast<char*>("-d"),
                        const_cast<char*>("monitor.db"), const_cast<char*>("-f"),
                        const_cast<char*>("hosts.txt"),  const_cast<char*>("-s")};
        REQUIRE(configManager.parseArguments(6, argv) == true);
        const auto& config = configManager.getConfig();
        REQUIRE(config.enableDatabase == true);
        REQUIRE(config.databasePath == "monitor.db");
        REQUIRE(config.filename == "hosts.txt");
        REQUIRE(config.silentMode == true);
    }
}

TEST_CASE("ConfigManager no arguments", "[config]") {
    SECTION("No arguments shows help") {
        reset_getopt();
        ConfigManager configManager;
        char* argv[] = {const_cast<char*>("mping")};
        REQUIRE(configManager.parseArguments(1, argv) == false);
    }
}