#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "constants.h"

/**
 * @brief Ping 管理器类，负责并发执行 ping 操作
 * 
 * 该类使用线程池实现并发 ping 操作，支持自定义并发数、ping 包数量和超时时间。
 * 使用 std::jthread 自动管理线程生命周期。
 */
class PingManager {
   private:
    // 默认最大并发数
    static const size_t DEFAULT_MAX_CONCURRENT = ConfigDefaults::MAX_CONCURRENT_PINGS;

   public:
    /**
     * @brief 执行 ping 操作并返回结果
     * 
     * @param hosts 主机列表，格式为 {IP: hostname}
     * @param pingCount 每个主机发送的 ping 包数量，默认为 3
     * @param timeoutSeconds 每个 ping 的超时时间（秒），默认为 3
     * @param maxConcurrent 最大并发数，默认为 50
     * 
     * @return std::vector<std::tuple<std::string, std::string, bool, short, std::string>>
     *         返回结果向量，每个元素包含 (IP, hostname, success, delay, timestamp)
     *         - IP: 主机 IP 地址
     *         - hostname: 主机名
     *         - success: ping 是否成功
     *         - delay: 最小延迟（毫秒）
     *         - timestamp: 时间戳（UTC 时间）
     * 
     * @note 此函数是线程安全的
     * @see pingHost
     */
    std::vector<std::tuple<std::string, std::string, bool, short, std::string>> performPing(
        const std::map<std::string, std::string>& hosts, int pingCount = 3, int timeoutSeconds = 3,
        size_t maxConcurrent = DEFAULT_MAX_CONCURRENT);
};

#endif  // PING_MANAGER_H