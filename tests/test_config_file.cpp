#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "config_file.h"

TEST_CASE("ConfigFile basic operations", "[config_file]") {
    SECTION("Create and save config file") {
        ConfigFile config;
        config.set("general", "database", "true");
        config.set("general", "ping_count", "5");
        config.setBool("general", "silent", false);

        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        REQUIRE(config.save(testPath) == true);
        REQUIRE(std::filesystem::exists(testPath) == true);

        std::filesystem::remove(testPath);
    }

    SECTION("Load config file") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "[general]\n";
        file << "database = true\n";
        file << "ping_count = 5\n";
        file << "silent = false\n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.isLoaded() == true);
        REQUIRE(config.getBool("general", "database") == true);
        REQUIRE(config.getInt("general", "ping_count") == 5);
        REQUIRE(config.getBool("general", "silent") == false);

        std::filesystem::remove(testPath);
    }

    SECTION("Get with default values") {
        ConfigFile config;
        REQUIRE(config.get("general", "missing", "default") == "default");
        REQUIRE(config.getInt("general", "missing", 42) == 42);
        REQUIRE(config.getBool("general", "missing", true) == true);
    }

    SECTION("Set and get values") {
        ConfigFile config;
        config.set("section1", "key1", "value1");
        config.setInt("section1", "key2", 123);
        config.setBool("section1", "key3", true);

        REQUIRE(config.get("section1", "key1") == "value1");
        REQUIRE(config.getInt("section1", "key2") == 123);
        REQUIRE(config.getBool("section1", "key3") == true);
    }

    SECTION("Remove config entry") {
        ConfigFile config;
        config.set("section1", "key1", "value1");
        REQUIRE(config.has("section1", "key1") == true);

        REQUIRE(config.remove("section1", "key1") == true);
        REQUIRE(config.has("section1", "key1") == false);
    }

    SECTION("Remove section") {
        ConfigFile config;
        config.set("section1", "key1", "value1");
        config.set("section1", "key2", "value2");

        REQUIRE(config.removeSection("section1") == true);
        REQUIRE(config.has("section1", "key1") == false);
        REQUIRE(config.has("section1", "key2") == false);
    }

    SECTION("Clear config") {
        ConfigFile config;
        config.set("section1", "key1", "value1");
        REQUIRE(config.has("section1", "key1") == true);

        config.clear();
        REQUIRE(config.has("section1", "key1") == false);
        REQUIRE(config.isLoaded() == false);
    }
}

TEST_CASE("ConfigFile XDG paths", "[config_file][xdg]") {
    SECTION("Get XDG config home") {
        std::string configHome = ConfigFile::getXDGConfigHome();
        REQUIRE(configHome.empty() == false);
    }

    SECTION("Get XDG data home") {
        std::string dataHome = ConfigFile::getXDGDataHome();
        REQUIRE(dataHome.empty() == false);
    }

    SECTION("Get XDG config dirs") {
        auto configDirs = ConfigFile::getXDGConfigDirs();
        REQUIRE(configDirs.empty() == false);
    }

    SECTION("Get default config paths") {
        auto paths = ConfigFile::getDefaultConfigPaths();
        REQUIRE(paths.empty() == false);
        REQUIRE(paths.size() >= 6);
    }

    SECTION("Create XDG config dir") {
        std::string configHome = ConfigFile::getXDGConfigHome();
        if (!configHome.empty()) {
            std::string mpingConfigDir = configHome + "/mping_test";

            // Clean up if exists
            if (std::filesystem::exists(mpingConfigDir)) {
                std::filesystem::remove_all(mpingConfigDir);
            }

            REQUIRE(ConfigFile::createXDGConfigDir() == true);

            // Clean up
            if (std::filesystem::exists(mpingConfigDir)) {
                std::filesystem::remove_all(mpingConfigDir);
            }
        }
    }
}

TEST_CASE("ConfigFile parsing", "[config_file]") {
    SECTION("Parse with comments") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "# This is a comment\n";
        file << "[general]\n";
        file << "database = true\n";
        file << "# inline comment\n";
        file << "; This is also a comment\n";
        file << "ping_count = 5\n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.getBool("general", "database") == true);
        REQUIRE(config.getInt("general", "ping_count") == 5);

        std::filesystem::remove(testPath);
    }

    SECTION("Parse with quoted values") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "[general]\n";
        file << "path = \"/path/to/file\"\n";
        file << "name = \"test name\"\n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.get("general", "path") == "/path/to/file");
        REQUIRE(config.get("general", "name") == "test name");

        std::filesystem::remove(testPath);
    }

    SECTION("Parse multiple sections") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "[general]\n";
        file << "ping_count = 5\n";
        file << "[database]\n";
        file << "enabled = true\n";
        file << "[alerts]\n";
        file << "days = 7\n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.getInt("general", "ping_count") == 5);
        REQUIRE(config.getBool("database", "enabled") == true);
        REQUIRE(config.getInt("alerts", "days") == 7);

        auto sections = config.getSections();
        REQUIRE(sections.size() == 3);

        std::filesystem::remove(testPath);
    }

    SECTION("Boolean parsing") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "[test]\n";
        file << "bool1 = true\n";
        file << "bool2 = false\n";
        file << "bool3 = yes\n";
        file << "bool4 = no\n";
        file << "bool5 = 1\n";
        file << "bool6 = 0\n";
        file << "bool7 = on\n";
        file << "bool8 = off\n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.getBool("test", "bool1") == true);
        REQUIRE(config.getBool("test", "bool2") == false);
        REQUIRE(config.getBool("test", "bool3") == true);
        REQUIRE(config.getBool("test", "bool4") == false);
        REQUIRE(config.getBool("test", "bool5") == true);
        REQUIRE(config.getBool("test", "bool6") == false);
        REQUIRE(config.getBool("test", "bool7") == true);
        REQUIRE(config.getBool("test", "bool8") == false);

        std::filesystem::remove(testPath);
    }

    SECTION("Parse with semicolon comments") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "; This is a semicolon comment\n";
        file << "[general]\n";
        file << "key = value\n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.get("general", "key") == "value");

        std::filesystem::remove(testPath);
    }

    SECTION("Parse with whitespace around values") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file << "  [general]  \n";
        file << "  key = value  \n";
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.get("general", "key") == "value");

        std::filesystem::remove(testPath);
    }

    SECTION("Empty file") {
        std::string testPath = "/tmp/test_config_XXXXXX.conf";
        int fd               = mkstemps(const_cast<char*>(testPath.c_str()), 5);
        REQUIRE(fd >= 0);
        close(fd);

        std::ofstream file(testPath);
        file.close();

        ConfigFile config;
        REQUIRE(config.load(testPath) == true);
        REQUIRE(config.getSections().empty());

        std::filesystem::remove(testPath);
    }

    SECTION("File not found") {
        ConfigFile config;
        REQUIRE(config.load("/tmp/nonexistent_file.conf") == false);
        REQUIRE(config.isLoaded() == false);
    }
}

// ══════════════════════════════════════════════════════════════════════════
//  ConfigFile 原子写入测试
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigFile atomic write — .tmp file cleanup", "[config_file][atomic]") {
    std::string testPath = "/tmp/test_atomic_XXXXXX.conf";
    int fd = mkstemps(const_cast<char*>(testPath.c_str()), 5);
    REQUIRE(fd >= 0);
    close(fd);

    ConfigFile config;
    config.set("section", "key", "value");
    REQUIRE(config.save(testPath));

    SECTION("Main file exists") {
        REQUIRE(std::filesystem::exists(testPath));
    }

    SECTION("Temp file is cleaned up") {
        std::string tmpPath = testPath + ".tmp";
        REQUIRE(std::filesystem::exists(tmpPath) == false);
    }

    SECTION("File content is correct") {
        std::ifstream file(testPath);
        REQUIRE(file.is_open());
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        // Should contain section header and key=value
        REQUIRE(content.find("[section]") != std::string::npos);
        REQUIRE(content.find("key") != std::string::npos);
        REQUIRE(content.find("value") != std::string::npos);
        // Should NOT contain ".tmp" in content
        REQUIRE(content.find(".tmp") == std::string::npos);
    }

    std::filesystem::remove(testPath);
}

// ══════════════════════════════════════════════════════════════════════════
//  ConfigFile 保存/加载双向验证
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigFile save/load roundtrip preserves all values", "[config_file][roundtrip]") {
    std::string testPath = "/tmp/test_roundtrip_XXXXXX.conf";
    int fd = mkstemps(const_cast<char*>(testPath.c_str()), 5);
    REQUIRE(fd >= 0);
    close(fd);

    SECTION("Single section") {
        ConfigFile config;
        config.set("general", "database", "true");
        config.setInt("general", "timeout", 30);
        config.setBool("general", "silent", true);
        REQUIRE(config.save(testPath));

        ConfigFile loaded;
        REQUIRE(loaded.load(testPath));
        REQUIRE(loaded.getBool("general", "database") == true);
        REQUIRE(loaded.getInt("general", "timeout") == 30);
        REQUIRE(loaded.getBool("general", "silent") == true);
    }

    SECTION("Multiple sections") {
        ConfigFile config;
        config.set("general", "ping_count", "5");
        config.set("database", "path", "/tmp/db");
        config.set("alerts", "enabled", "true");
        config.set("alerts", "days", "7");
        REQUIRE(config.save(testPath));

        ConfigFile loaded;
        REQUIRE(loaded.load(testPath));
        REQUIRE(loaded.getInt("general", "ping_count") == 5);
        REQUIRE(loaded.get("database", "path") == "/tmp/db");
        REQUIRE(loaded.getBool("alerts", "enabled") == true);
        REQUIRE(loaded.getInt("alerts", "days") == 7);
    }

    SECTION("Roundtrip preserves all keys") {
        ConfigFile config;
        config.set("a", "k1", "v1");
        config.set("a", "k2", "v2");
        config.set("b", "k3", "v3");
        REQUIRE(config.save(testPath));

        ConfigFile loaded;
        REQUIRE(loaded.load(testPath));
        auto sections = loaded.getSections();
        REQUIRE(sections.size() == 2);
        auto keysA = loaded.getKeys("a");
        REQUIRE(keysA.size() == 2);
        auto keysB = loaded.getKeys("b");
        REQUIRE(keysB.size() == 1);
    }

    std::filesystem::remove(testPath);
}

TEST_CASE("ConfigFile save preserves original path", "[config_file][save]") {
    std::string testPath = "/tmp/test_origpath_XXXXXX.conf";
    int fd = mkstemps(const_cast<char*>(testPath.c_str()), 5);
    REQUIRE(fd >= 0);
    close(fd);

    ConfigFile config;
    config.set("section", "key", "value");
    REQUIRE(config.save(testPath));
    REQUIRE(config.getFilePath() == testPath);

    // save() without args should use the original path
    config.set("section", "key2", "value2");
    REQUIRE(config.save());
    REQUIRE(std::filesystem::exists(testPath));

    // Reload and verify
    ConfigFile loaded;
    REQUIRE(loaded.load(testPath));
    REQUIRE(loaded.get("section", "key") == "value");
    REQUIRE(loaded.get("section", "key2") == "value2");

    std::filesystem::remove(testPath);
}