#ifndef CONSTANTS_H
#define CONSTANTS_H

/**
 * @brief 配置默认值常量
 *
 * 集中定义所有默认配置值，避免模块间的编译依赖。
 */
struct ConfigDefaults {
    static constexpr int MAX_CONCURRENT_PINGS   = 50;  // 最大并发 ping 数
    static constexpr int DEFAULT_CLEANUP_DAYS   = 30;  // 默认清理天数
    static constexpr int FIRST_ROUND_PING_COUNT = 1;   // 第一轮 ping 包数量
    static constexpr int RETRY_ROUND_PING_COUNT = 5;   // 第二轮重试 ping 包数量
    static constexpr int FIRST_ROUND_TIMEOUT    = 1;   // 第一轮超时时间（秒）
    static constexpr int RETRY_ROUND_TIMEOUT    = 3;   // 第二轮重试超时时间（秒）
    // 新增告警前的确认重试轮数：首次不通时先重试确认，避免瞬时故障误报告警
    static constexpr int ALERT_CONFIRM_RETRY_COUNT = 3;

    // 文件名默认值
    static constexpr const char* DEFAULT_FILENAME = "ip.txt";
    // ping 结果默认保留天数
    static constexpr int DEFAULT_PING_RESULTS_CLEANUP_DAYS = 30;

    // 查询模式的特殊标记
    static constexpr int QUERY_MODE_ENABLED_NO_DAYS = -2;  // 已启用但未指定天数
};

#endif  // CONSTANTS_H
