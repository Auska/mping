#include "schema_migration.h"

#include <libpq-fe.h>

#include <cctype>
#include <iostream>
#include <print>
#include <stdexcept>

#include "database_manager_pg.h"

// ─── UTC 日期工具 ──────────────────────────────────────────────────────

time_t utcDayFloor(time_t t) {
    return (t / 86400) * 86400;
}

int daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era      = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

time_t parseUtcDate(const std::string& ymd) {
    if (ymd.size() != 10 || ymd[4] != '-' || ymd[7] != '-') {
        return -1;
    }
    for (size_t i = 0; i < ymd.size(); i++) {
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(ymd[i]))) {
            return -1;
        }
    }
    const int y = std::stoi(ymd.substr(0, 4));
    const int m = std::stoi(ymd.substr(5, 2));
    const int d = std::stoi(ymd.substr(8, 2));
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        return -1;
    }
    return static_cast<time_t>(daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d)))
           * 86400;
}

std::string formatUtcTime(time_t t, const char* fmt) {
    struct tm tmBuf{};
    gmtime_r(&t, &tmBuf);
    char buf[32];
    strftime(buf, sizeof(buf), fmt, &tmBuf);
    return buf;
}

// ─── ping_results 按日分区与迁移 ───────────────────────────────────────

std::string SchemaMigrator::tableRelkind(DatabaseManagerPG& db, const std::string& name) {
    const std::string sql =
        "SELECT relkind::text FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace "
        "WHERE n.nspname = 'public' AND c.relname = '"
        + name + "'";
    PGresult* res = db.executeQueryWithResult(sql);
    if (!res) {
        return {};
    }
    std::string relkind = PQntuples(res) > 0 ? PQgetvalue(res, 0, 0) : "";
    PQclear(res);
    return relkind;
}

bool SchemaMigrator::createPartitionForDay(DatabaseManagerPG& db, time_t day) {
    const std::string d1  = formatUtcTime(day, "%Y-%m-%d");
    const std::string d2  = formatUtcTime(day + 86400, "%Y-%m-%d");
    const std::string nm  = formatUtcTime(day, "%Y%m%d");
    const std::string sql = "CREATE TABLE IF NOT EXISTS ping_results_" + nm
                            + " PARTITION OF ping_results FOR VALUES FROM ('" + d1
                            + " 00:00:00') TO ('" + d2 + " 00:00:00')";
    return db.executeQuery(sql);
}

bool SchemaMigrator::createPartitionForTimestamp(DatabaseManagerPG& db,
                                                 const std::string& timestamp) {
    // 会话时区固定为 UTC，时间戳文本前 10 个字符即该行的 UTC 日期
    if (timestamp.size() < 10) {
        return false;
    }
    const time_t day = parseUtcDate(timestamp.substr(0, 10));
    if (day < 0) {
        return false;
    }
    return createPartitionForDay(db, day);
}

bool SchemaMigrator::ensurePingResultsPartitions(DatabaseManagerPG& db, int lookaheadDays) {
    // 尚未分区（如表不存在或迁移失败）时无需预建，插入时的补建逻辑兜底
    if (tableRelkind(db, "ping_results") != "p") {
        return true;
    }
    const time_t day = utcDayFloor(time(nullptr));
    for (int i = 0; i < lookaheadDays; i++) {
        if (!createPartitionForDay(db, day + static_cast<time_t>(i) * 86400)) {
            return false;
        }
    }
    return true;
}

bool SchemaMigrator::migratePingResultsPartitioning(DatabaseManagerPG& db) {
    // 1. 旧版普通表：改名保留数据（序列一并改名，避免与新分区表的 SERIAL 序列同名冲突）；
    //    幂等：重复执行时表已不叫 ping_results，自动跳过
    if (tableRelkind(db, "ping_results") == "r") {
        if (!db.executeQuery("ALTER TABLE ping_results RENAME TO ping_results_legacy")) {
            return false;
        }
        if (!db.executeQuery("ALTER SEQUENCE IF EXISTS ping_results_id_seq RENAME TO "
                             "ping_results_legacy_id_seq")) {
            return false;
        }
    }

    // 2. 创建按日 RANGE 分区表（幂等）；PRIMARY KEY 必须包含分区键
    const char* createPingResultsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS ping_results (
            id SERIAL,
            ip TEXT NOT NULL,
            hostname TEXT,
            delay INTEGER,
            success BOOLEAN,
            timestamp TIMESTAMPTZ NOT NULL,
            PRIMARY KEY (id, timestamp)
        ) PARTITION BY RANGE (timestamp);
    )";
    if (!db.executeQuery(createPingResultsTableSQL)) {
        std::println(std::cerr, "Failed to create ping_results table");
        return false;
    }

    // 分区索引：建在父表上，自动传播到每个分区
    const char* createPingResultsIndexSQL = R"(
        CREATE INDEX IF NOT EXISTS idx_ping_results_ip ON ping_results (ip);
        CREATE INDEX IF NOT EXISTS idx_ping_results_timestamp ON ping_results (timestamp);
    )";
    if (!db.executeQuery(createPingResultsIndexSQL)) {
        std::println(std::cerr, "Failed to create indexes for ping_results table");
        return false;
    }

    // 3. 回填旧表数据（幂等：中断后重试不产生重复行）；旧表及旧序列随后清除
    if (tableRelkind(db, "ping_results_legacy") == "r") {
        PGresult* range = db.executeQueryWithResult(
            "SELECT MIN(timestamp), MAX(timestamp) FROM ping_results_legacy");
        if (!range) {
            return false;
        }
        // 为旧数据覆盖的每一天补建分区（会话 UTC，时间戳文本前 10 字符即 UTC 日期）
        if (PQntuples(range) > 0 && !PQgetisnull(range, 0, 0)) {
            const time_t minDay = parseUtcDate(std::string(PQgetvalue(range, 0, 0)).substr(0, 10));
            const time_t maxDay = parseUtcDate(std::string(PQgetvalue(range, 0, 1)).substr(0, 10));
            if (minDay < 0 || maxDay < 0) {
                PQclear(range);
                return false;
            }
            for (time_t day = minDay; day <= maxDay; day += 86400) {
                if (!createPartitionForDay(db, day)) {
                    PQclear(range);
                    return false;
                }
            }
        }
        PQclear(range);

        // 复制数据（保留原 id），ON CONFLICT 保证失败重跑不重复
        const char* copySQL = R"(
            INSERT INTO ping_results (id, ip, hostname, delay, success, timestamp)
            SELECT id, ip, hostname, delay, success, timestamp FROM ping_results_legacy
            ON CONFLICT DO NOTHING;
        )";
        if (!db.executeQuery(copySQL)) {
            return false;
        }

        // 序列推进到现有最大 id 之后
        if (!db.executeQuery("SELECT setval('ping_results_id_seq', "
                             "COALESCE((SELECT MAX(id) + 1 FROM ping_results), 1), false)")) {
            return false;
        }

        if (!db.executeQuery("DROP TABLE ping_results_legacy")) {
            return false;
        }
        std::println(std::cout, "Migrated ping_results to daily-partitioned layout");
    }
    return true;
}

bool SchemaMigrator::migrateSchema(DatabaseManagerPG& db) {
    // 确保会话时区为 UTC，这样现有 TIMESTAMP 数据会被正确解释为 UTC 时间
    if (!db.executeQuery("SET TIME ZONE 'UTC';")) {
        std::println(std::cerr, "Failed to set timezone to UTC for migration");
        return false;
    }

    // 查询所有需要迁移的列：类型为 timestamp without time zone 的列
    const char* checkSQL = R"(
        SELECT table_name, column_name
        FROM information_schema.columns
        WHERE table_schema = 'public'
          AND data_type = 'timestamp without time zone'
          AND table_name IN ('hosts', 'ping_results', 'alerts', 'recovery_records')
        ORDER BY table_name, column_name;
    )";

    PGresult* res = db.executeQueryWithResult(checkSQL);
    if (!res) {
        std::println(std::cerr, "Failed to check schema for migration");
        return false;
    }

    int colCount = PQntuples(res);
    if (colCount == 0) {
        PQclear(res);
        return true;  // 没有需要迁移的列
    }

    std::println(std::cout, "Migrating {} timestamp column(s) to timestamptz...", colCount);

    for (int i = 0; i < colCount; i++) {
        std::string table  = PQgetvalue(res, i, 0);
        std::string column = PQgetvalue(res, i, 1);

        std::string alterSQL =
            "ALTER TABLE " + table + " ALTER COLUMN " + column + " TYPE TIMESTAMPTZ";
        std::println(std::cout, "  Migrating {}.{}...", table, column);

        if (!db.executeQuery(alterSQL)) {
            std::println(std::cerr, "Failed to migrate {}.{} to TIMESTAMPTZ", table, column);
            PQclear(res);
            return false;
        }
    }

    PQclear(res);
    std::println(std::cout, "Schema migration completed.");
    return true;
}
