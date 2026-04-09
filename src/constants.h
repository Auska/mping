#ifndef CONSTANTS_H
#define CONSTANTS_H

/**
 * @brief 配置默认值常量
 *
 * 集中定义所有默认配置值，避免模块间的编译依赖。
 */
struct ConfigDefaults {
    static constexpr int MAX_CONCURRENT_PINGS = 50;   // 最大并发 ping 数
    static constexpr int DEFAULT_CLEANUP_DAYS  = 30;  // 默认清理天数
    static constexpr int DEFAULT_PING_COUNT    = 3;   // 默认 ping 包数量
    static constexpr int DEFAULT_TIMEOUT       = 3;   // 默认超时时间（秒）
};

#endif  // CONSTANTS_H
