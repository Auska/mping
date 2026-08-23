#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include <map>
#include <optional>
#include <string>

class ConfigFile {
   private:
    std::map<std::string, std::map<std::string, std::string>> configData;
    std::string filePath;

    // 去除字符串两端的空白字符
    std::string trim(const std::string& str) const;

    // 解析一行配置
    bool parseLine(const std::string& line, std::string& currentSection);

   public:
    ConfigFile()                        = default;
    ~ConfigFile()                       = default;
    ConfigFile(ConfigFile&&)            = default;
    ConfigFile& operator=(ConfigFile&&) = default;

    // 从指定路径加载配置文件
    bool load(const std::string& path);

    // 保存配置文件到指定路径
    bool save(const std::string& path);

    // 保存到原路径
    bool save();

    // 获取配置值
    std::optional<std::string> get(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue) const;

    // 获取整数配置值
    std::optional<int> getInt(const std::string& section, const std::string& key) const;
    int getInt(const std::string& section, const std::string& key, int defaultValue) const;

    // 获取布尔配置值
    std::optional<bool> getBool(const std::string& section, const std::string& key) const;
    bool getBool(const std::string& section, const std::string& key, bool defaultValue) const;

    // 设置配置值
    void set(const std::string& section, const std::string& key, const std::string& value);
    void setInt(const std::string& section, const std::string& key, int value);
    void setBool(const std::string& section, const std::string& key, bool value);

    // 检查配置是否存在
    bool has(const std::string& section, const std::string& key) const;

    // 获取配置文件路径
    const std::string& getFilePath() const noexcept;

    // 获取默认配置文件路径（$HOME/.config/mping/config.ini，HOME 未设置时返回空串）
    static std::string getDefaultConfigPath();
};

#endif  // CONFIG_FILE_H
