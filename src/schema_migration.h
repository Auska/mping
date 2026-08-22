#ifndef SCHEMA_MIGRATION_H
#define SCHEMA_MIGRATION_H

#include <ctime>
#include <string>

class DatabaseManagerPG;

// ═══ UTC 日期工具（分区迁移与清理共用）═══

// 对齐到 UTC 日界（UTC 无夏令时，time_t 按 86400 整除即当日零点）
time_t utcDayFloor(time_t t);

// 公历日 → 1970-01-01 起天数（Hinnant 算法，纯标准 C++）
int daysFromCivil(int y, unsigned m, unsigned d);

// "YYYY-MM-DD" → UTC 日界秒；格式非法返回 -1
time_t parseUtcDate(const std::string& ymd);

// UTC 时间格式化为文本（gmtime_r 线程安全）
std::string formatUtcTime(time_t t, const char* fmt);

// ═══ ping_results 分区与旧库迁移 ═══
// friend of DatabaseManagerPG：经由其私有 executeQuery/executeQueryWithResult 执行 SQL

class SchemaMigrator {
   public:
    // 旧版普通表迁移为按日分区表（改名保留数据、回填，幂等）
    static bool migratePingResultsPartitioning(DatabaseManagerPG& db);
    // 将旧 TIMESTAMP 列迁移为 TIMESTAMPTZ
    static bool migrateSchema(DatabaseManagerPG& db);
    // 为今天起 lookaheadDays 天预建日分区
    static bool ensurePingResultsPartitions(DatabaseManagerPG& db, int lookaheadDays);
    // 由时间戳文本解析其 UTC 日期并补建分区（插入遇 23514 时调用）
    static bool createPartitionForTimestamp(DatabaseManagerPG& db, const std::string& timestamp);

   private:
    // 查询 public 模式下表的 relkind；不存在返回空串
    static std::string tableRelkind(DatabaseManagerPG& db, const std::string& name);
    // 为 UTC 日 day 建分区（幂等）
    static bool createPartitionForDay(DatabaseManagerPG& db, time_t day);
};

#endif  // SCHEMA_MIGRATION_H
