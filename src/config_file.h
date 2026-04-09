#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include <map>
#include <optional>
#include <string>
#include <vector>

class ConfigFile {
   public:
    // 配置节和键
    struct ConfigEntry {
        std::string section;
        std::string key;
        std::string value;
    };

   private:
    std::map<std::string, std::map<std::string, std::string>> configData;
    std::vector<ConfigEntry> originalEntries;
    std::string filePath;
    bool loaded;

    // 去除字符串两端的空白字符
    std::string trim(const std::string& str) const;

    // 解析一行配置
    bool parseLine(const std::string& line, std::string& currentSection);

   public:
    ConfigFile();
    ~ConfigFile();

    // 从指定路径加载配置文件
    bool load(const std::string& path);

    // 按照 XDG 规范自动查找并加载配置文件
    bool loadFromXDGPaths();

    // 保存配置文件到指定路径
    bool save(const std::string& path);

    // 保存到原路径
    bool save();

    // 获取配置值
    std::optional<std::string> get(const std::string& section, const std::string& key) const;

    // 获取配置值，带默认值
    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue) const;

    // 获取整数配置值
    std::optional<int> getInt(const std::string& section, const std::string& key) const;

    // 获取整数配置值，带默认值
    int getInt(const std::string& section, const std::string& key, int defaultValue) const;

    // 获取布尔配置值
    std::optional<bool> getBool(const std::string& section, const std::string& key) const;

    // 获取布尔配置值，带默认值
    bool getBool(const std::string& section, const std::string& key, bool defaultValue) const;

    // 设置配置值
    void set(const std::string& section, const std::string& key, const std::string& value);

    // 设置整数配置值
    void setInt(const std::string& section, const std::string& key, int value);

    // 设置布尔配置值
    void setBool(const std::string& section, const std::string& key, bool value);

    // 检查配置是否存在
    bool has(const std::string& section, const std::string& key) const;

    // 获取所有节
    std::vector<std::string> getSections() const;

    // 获取指定节的所有键
    std::vector<std::string> getKeys(const std::string& section) const;

    // 删除配置项
    bool remove(const std::string& section, const std::string& key);

    // 删除整个节
    bool removeSection(const std::string& section);

    // 清空所有配置
    void clear();

    // 检查是否已加载
    bool isLoaded() const;

    // 获取配置文件路径
    std::string getFilePath() const;

    // 获取 XDG 配置目录路径
    static std::string getXDGConfigHome();

    // 获取 XDG 数据目录路径
    static std::string getXDGDataHome();

    // 获取 XDG 配置文件搜索路径列表
    static std::vector<std::string> getXDGConfigDirs();

    // 获取默认配置文件路径列表（按优先级排序）
    static std::vector<std::string> getDefaultConfigPaths();

    // 创建 XDG 配置目录（如果不存在）
    static bool createXDGConfigDir();
};

#endif  // CONFIG_FILE_H