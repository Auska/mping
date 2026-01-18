#ifndef DATABASE_INTERFACE_H
#define DATABASE_INTERFACE_H

#include <map>
#include <string>
#include <tuple>
#include <vector>

class DatabaseInterface {
   public:
    virtual ~DatabaseInterface() = default;

    // 初始化数据库
    virtual bool initialize() = 0;

    // 插入单个ping结果
    virtual bool insertPingResult(const std::string& ip, const std::string& hostname, short delay,
                                  bool success, const std::string& timestamp) = 0;

    // 批量插入ping结果
    virtual bool insertPingResults(
        const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>&
            results) = 0;

    // 查询IP统计信息
    virtual void queryIPStatistics(const std::string& ip) = 0;

    // 清理旧数据
    virtual void cleanupOldData(int days) = 0;

    // 获取所有主机
    virtual std::map<std::string, std::string> getAllHosts() = 0;

    // 添加告警
    virtual bool addAlert(const std::string& ip, const std::string& hostname) = 0;

    // 移除告警
    virtual bool removeAlert(const std::string& ip) = 0;

    // 获取活动告警
    virtual std::vector<std::tuple<std::string, std::string, std::string>> getActiveAlerts(
        int days = -1) = 0;

    // 获取恢复记录
    virtual std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
    getRecoveryRecords(int days = -1) = 0;
};

#endif  // DATABASE_INTERFACE_H