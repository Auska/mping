#include "config_file.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

std::string ConfigFile::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

bool ConfigFile::parseLine(const std::string& line, std::string& currentSection) {
    std::string trimmed = trim(line);

    // 跳过空行和注释
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
        return true;
    }

    // 解析节 [section]
    if (trimmed[0] == '[' && trimmed[trimmed.length() - 1] == ']') {
        currentSection = trim(trimmed.substr(1, trimmed.length() - 2));
        return true;
    }

    // 解析键值对 key = value
    size_t equalPos = trimmed.find('=');
    if (equalPos != std::string::npos) {
        std::string key   = trim(trimmed.substr(0, equalPos));
        std::string value = trim(trimmed.substr(equalPos + 1));

        // 处理引号
        if (value.length() >= 2 && value[0] == '"' && value[value.length() - 1] == '"') {
            value = value.substr(1, value.length() - 2);
        }

        if (!currentSection.empty() && !key.empty()) {
            configData[currentSection][key] = value;
        }
    }

    return true;
}

bool ConfigFile::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    filePath = path;
    configData.clear();

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        if (!parseLine(line, currentSection)) {
            return false;
        }
    }
    return true;
}

bool ConfigFile::loadFromXDGPaths() {
    for (const auto& path : getDefaultConfigPaths()) {
        if (load(path)) {
            return true;
        }
    }
    return false;
}

bool ConfigFile::save(const std::string& path) {
    // 确保目录存在
    std::filesystem::path filePath(path);
    std::filesystem::path dirPath = filePath.parent_path();

    if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
        try {
            std::filesystem::create_directories(dirPath);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create directory: " << dirPath << std::endl;
            return false;
        }
    }

    // 原子写入：先写临时文件，再 rename
    std::string tmpPath = path + ".tmp";
    std::ofstream file(tmpPath);
    if (!file.is_open()) {
        return false;
    }

    file << "# mping configuration file" << std::endl;
    file << "# Generated automatically" << std::endl;
    file << std::endl;

    // 按节写出全部条目（map 有序；未知键随 load 保留，注释与顺序不保留）
    for (const auto& [section, keyMap] : configData) {
        file << "[" << section << "]" << std::endl;
        for (const auto& [key, value] : keyMap) {
            file << key << " = \"" << value << "\"" << std::endl;
        }
        file << std::endl;
    }

    file.close();

    // 原子替换（POSIX 保证：同文件系统内 rename 是原子的）
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::cerr << "Failed to atomically replace config file: " << ec.message() << std::endl;
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    this->filePath = path;
    return true;
}

bool ConfigFile::save() {
    if (filePath.empty()) {
        return false;
    }
    return save(filePath);
}

std::optional<std::string> ConfigFile::get(const std::string& section,
                                           const std::string& key) const {
    auto sectionIt = configData.find(section);
    if (sectionIt == configData.end()) {
        return std::nullopt;
    }

    auto keyIt = sectionIt->second.find(key);
    if (keyIt == sectionIt->second.end()) {
        return std::nullopt;
    }

    return keyIt->second;
}

std::string ConfigFile::get(const std::string& section, const std::string& key,
                            const std::string& defaultValue) const {
    auto value = get(section, key);
    return value ? *value : defaultValue;
}

std::optional<int> ConfigFile::getInt(const std::string& section, const std::string& key) const {
    auto value = get(section, key);
    if (!value) {
        return std::nullopt;
    }

    try {
        return std::stoi(*value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int ConfigFile::getInt(const std::string& section, const std::string& key, int defaultValue) const {
    auto value = getInt(section, key);
    return value ? *value : defaultValue;
}

std::optional<bool> ConfigFile::getBool(const std::string& section, const std::string& key) const {
    auto value = get(section, key);
    if (!value) {
        return std::nullopt;
    }

    std::string lowerValue = *value;
    std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerValue == "true" || lowerValue == "yes" || lowerValue == "1" || lowerValue == "on") {
        return true;
    } else if (lowerValue == "false" || lowerValue == "no" || lowerValue == "0"
               || lowerValue == "off") {
        return false;
    }

    return std::nullopt;
}

bool ConfigFile::getBool(const std::string& section, const std::string& key,
                         bool defaultValue) const {
    auto value = getBool(section, key);
    return value ? *value : defaultValue;
}

void ConfigFile::set(const std::string& section, const std::string& key, const std::string& value) {
    configData[section][key] = value;
}

void ConfigFile::setInt(const std::string& section, const std::string& key, int value) {
    set(section, key, std::to_string(value));
}

void ConfigFile::setBool(const std::string& section, const std::string& key, bool value) {
    set(section, key, value ? "true" : "false");
}

bool ConfigFile::has(const std::string& section, const std::string& key) const {
    auto sectionIt = configData.find(section);
    if (sectionIt == configData.end()) {
        return false;
    }
    return sectionIt->second.find(key) != sectionIt->second.end();
}

const std::string& ConfigFile::getFilePath() const noexcept {
    return filePath;
}

std::string ConfigFile::getXDGConfigHome() {
    const char* env = std::getenv("XDG_CONFIG_HOME");
    if (env && env[0] != '\0') {
        return env;
    }

    // 默认值：$HOME/.config
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.config";
    }

    return "";
}

std::vector<std::string> ConfigFile::getXDGConfigDirs() {
    std::vector<std::string> dirs;

    const char* env = std::getenv("XDG_CONFIG_DIRS");
    if (env && env[0] != '\0') {
        std::stringstream ss(env);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty()) {
                dirs.push_back(dir);
            }
        }
    }

    // 默认值：/etc/xdg
    if (dirs.empty()) {
        dirs.push_back("/etc/xdg");
    }

    return dirs;
}

std::vector<std::string> ConfigFile::getDefaultConfigPaths() {
    std::vector<std::string> paths;

    // 1. $XDG_CONFIG_HOME/mping/config
    std::string configHome = getXDGConfigHome();
    if (!configHome.empty()) {
        paths.push_back(configHome + "/mping/config");
    }

    // 2. $XDG_CONFIG_DIRS/mping/config
    for (const auto& dir : getXDGConfigDirs()) {
        paths.push_back(dir + "/mping/config");
    }

    // 3. ~/.config/mping/config
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        paths.push_back(std::string(home) + "/.config/mping/config");
    }

    // 4. ~/.mpingrc
    if (home && home[0] != '\0') {
        paths.push_back(std::string(home) + "/.mpingrc");
    }

    // 5. ./mping.conf
    paths.push_back("mping.conf");

    // 6. ./.mpingrc
    paths.push_back(".mpingrc");

    return paths;
}

bool ConfigFile::createXDGConfigDir() {
    std::string configHome = getXDGConfigHome();
    if (configHome.empty()) {
        return false;
    }

    std::string mpingConfigDir = configHome + "/mping";

    try {
        if (!std::filesystem::exists(mpingConfigDir)) {
            std::filesystem::create_directories(mpingConfigDir);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create XDG config directory: " << mpingConfigDir << std::endl;
        return false;
    }
}
