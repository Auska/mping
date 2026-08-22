#ifndef CONSTANTS_H
#define CONSTANTS_H

/**
 * @brief 配置默认值常量
 *
 * 集中定义所有默认配置值，避免模块间的编译依赖。
 */
struct ConfigDefaults {
    static constexpr int MAX_CONCURRENT_PINGS = 50;  // 最大并发 ping 数
    static constexpr int DEFAULT_CLEANUP_DAYS = 30;  // 默认清理天数
    // 滑动窗口检查参数：每轮每主机发 CHECK_ROUND_PING_COUNT 个探测包（任一成功即本轮成功），
    // 连续 DOWN_CONFIRM_WINDOW 轮全部失败才判定离线，对抗瞬时丢包/网络波动。
    // 取代原先"第一轮快速探测 + 失败重试 + 告警确认重试"的重叠流程。
    static constexpr int CHECK_ROUND_PING_COUNT = 3;  // 每轮探测包数
    static constexpr int CHECK_ROUND_TIMEOUT    = 1;  // 每包超时（秒）
    static constexpr int DOWN_CONFIRM_WINDOW    = 3;  // 连续失败判定离线的轮数窗口

    // 文件名默认值
    static constexpr const char* DEFAULT_FILENAME = "ip.txt";
    // ping 结果默认保留天数
    static constexpr int DEFAULT_PING_RESULTS_CLEANUP_DAYS = 30;
    // ping_results 按日分区（UTC 日界）：initialize 时预建今天起 N 天的分区；
    // 更早日期（停机恢复、历史回填）在插入遇到 23514 时自动补建
    static constexpr int PING_PARTITION_LOOKAHEAD_DAYS = 30;

    // 查询模式的特殊标记
    static constexpr int QUERY_MODE_ENABLED_NO_DAYS = -2;  // 已启用但未指定天数
};

#endif  // CONSTANTS_H
