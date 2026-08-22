#include "database_manager_pg.h"

#include <cctype>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <print>
#include <sstream>
#include <stdexcept>

#include "constants.h"
#include "ip_validator.h"
#include "schema_migration.h"

namespace {

// 统计信息与最近记录输出辅助
void printHeader(const std::string& ip, const std::string& hostname) {
    std::println(std::cout, "Statistics for IP: {} ({})", ip, hostname);
    std::println(std::cout, "=========================================================");
}

void printBody(const PingStatistics& stats) {
    std::println(std::cout, "Total ping records: {}", stats.totalRecords);
    if (stats.totalRecords == 0) {
        std::println(std::cout, "No ping records found for this IP.");
        return;
    }
    std::println(std::cout, "Successful pings: {}", stats.successCount);
    std::println(std::cout, "Failed pings: {}", stats.failureCount);
    std::println(std::cout, "Success rate: {:.2f}%", stats.successRate);
    std::println(std::cout, "Failure rate: {:.2f}%", stats.failureRate);
    std::println(std::cout, "Average delay (successful pings): {:.2f}ms", stats.avgDelay);
    std::println(std::cout, "Maximum delay (successful pings): {}ms", stats.maxDelay);
    std::println(std::cout, "Minimum delay (successful pings): {}ms", stats.minDelay);
}

void printRecentRecordsHeader() {
    std::println(std::cout, "\nRecent ping records (last 10):");
    std::println(std::cout, "Timestamp           \tDelay\tStatus");
    std::println(std::cout, "--------------------------------------------------------");
}

void printRecentRecordRow(const std::string& timestamp, int delay, bool success) {
    std::println(std::cout, "{}\t{}ms\t{}", timestamp, delay, success ? "Success" : "Failed");
}

}  // namespace

DatabaseManagerPG::DatabaseManagerPG(const std::string& connectionInfo)
    : connInfo(connectionInfo), conn(nullptr) {
    if (connectionInfo.empty()) {
        throw std::invalid_argument("Database connection info cannot be empty");
    }
}

bool DatabaseManagerPG::ensureConnected() {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    if (PQstatus(conn.get()) == CONNECTION_OK) {
        return true;
    }

    // 连接断开则尝试重新连接
    std::println(std::cerr, "Database connection lost. Attempting to reconnect...");
    PQfinish(conn.get());
    PGconn* rawConn = PQconnectdb(connInfo.c_str());
    if (rawConn == nullptr) {
        std::println(std::cerr, "Failed to allocate database connection (out of memory)");
        return false;
    }
    conn.reset(rawConn);

    if (PQstatus(conn.get()) != CONNECTION_OK) {
        std::println(std::cerr, "Failed to reconnect to database: {}", PQerrorMessage(conn.get()));
        return false;
    }
    return true;
}

bool DatabaseManagerPG::executeQuery(const std::string& query) {
    if (!ensureConnected()) {
        return false;
    }

    PGresult* res = PQexec(conn.get(), query.c_str());
    if (res == nullptr) {
        std::println(std::cerr, "Query failed: out of memory");
        return false;
    }
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
}

PGresult* DatabaseManagerPG::executeQueryWithResult(const std::string& query) {
    if (!ensureConnected()) {
        return nullptr;
    }

    PGresult* res = PQexec(conn.get(), query.c_str());
    if (res == nullptr) {
        std::println(std::cerr, "Query failed: out of memory");
        return nullptr;
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Query failed: {}", PQresultErrorMessage(res));
        PQclear(res);
        return nullptr;
    }
    return res;
}

PGresult* DatabaseManagerPG::execParams(const std::string& sql,
                                        const std::vector<std::string>& params) {
    std::vector<const char*> values;
    std::vector<int> lengths;
    values.reserve(params.size());
    lengths.reserve(params.size());
    for (const auto& p : params) {
        values.push_back(p.c_str());
        lengths.push_back(static_cast<int>(p.length()));
    }
    std::vector<int> formats(params.size(), 0);
    return PQexecParams(conn.get(), sql.c_str(), static_cast<int>(params.size()), nullptr,
                        values.data(), lengths.data(), formats.data(), 0);
}

bool DatabaseManagerPG::connectSession() {
    PGconn* rawConn = PQconnectdb(connInfo.c_str());
    if (rawConn == nullptr) {
        std::println(std::cerr, "Failed to allocate database connection (out of memory)");
        return false;
    }
    conn.reset(rawConn);

    if (PQstatus(conn.get()) != CONNECTION_OK) {
        std::println(std::cerr, "Failed to connect to database: {}", PQerrorMessage(conn.get()));
        return false;
    }

    // 设置client_min_messages参数以抑制NOTICE消息
    // 检查连接字符串中是否包含client_min_messages参数
    if (connInfo.find("client_min_messages") == std::string::npos) {
        PGresult* res = PQexec(conn.get(), "SET client_min_messages TO WARNING;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::println(std::cerr, "Failed to set client_min_messages: {}",
                         PQresultErrorMessage(res));
            PQclear(res);
            return false;
        }
        PQclear(res);
    }

    // 会话统一按 UTC 解释时间戳：ping_results 的时间以 UTC 文本写入，
    // 必须固定会话时区，否则会受服务器默认时区影响而产生偏移（与迁移逻辑一致）
    PGresult* tzRes = PQexec(conn.get(), "SET TIME ZONE 'UTC';");
    if (PQresultStatus(tzRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to set timezone to UTC: {}", PQresultErrorMessage(tzRes));
        PQclear(tzRes);
        return false;
    }
    PQclear(tzRes);

    // 设置连接保持活动状态
    PGresult* res = PQexec(conn.get(), "SET tcp_keepalives_idle = 60;");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Warning: Failed to set tcp_keepalives_idle: {}",
                     PQresultErrorMessage(res));
    }
    PQclear(res);
    return true;
}

// 建表 + 旧库迁移（幂等）：查询命令与写入命令共用；不预建分区
bool DatabaseManagerPG::prepareSchema() {
    // 创建hosts表，用于存储IP地址与主机名的映射关系
    const char* createHostsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS hosts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMPTZ DEFAULT NOW(),
            last_seen TIMESTAMPTZ,
            last_status BOOLEAN,
            last_delay INTEGER
        );
    )";

    if (!executeQuery(createHostsTableSQL)) {
        std::println(std::cerr, "Failed to create hosts table");
        return false;
    }

    // ping_results 按日 RANGE 分区表：旧版普通表先迁移（改名保留数据后回填）
    if (!SchemaMigrator::migratePingResultsPartitioning(*this)) {
        std::println(std::cerr, "Failed to create ping_results partition table");
        return false;
    }

    // 创建alerts表，用于存储告警信息
    const char* createAlertsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS alerts (
            ip TEXT PRIMARY KEY,
            hostname TEXT,
            created_time TIMESTAMPTZ
        );
    )";

    if (!executeQuery(createAlertsTableSQL)) {
        std::println(std::cerr, "Failed to create alerts table");
        return false;
    }

    // 创建recovery_records表，用于存储恢复记录
    const char* createRecoveryRecordsTableSQL = R"(
        CREATE TABLE IF NOT EXISTS recovery_records (
            id SERIAL PRIMARY KEY,
            ip TEXT,
            hostname TEXT,
            alert_time TIMESTAMPTZ,
            recovery_time TIMESTAMPTZ
        );
    )";

    if (!executeQuery(createRecoveryRecordsTableSQL)) {
        std::println(std::cerr, "Failed to create recovery_records table");
        return false;
    }

    // 创建mping_meta表，存储内部元数据（如自动清理节流标记）
    const char* createMetaTableSQL = R"(
        CREATE TABLE IF NOT EXISTS mping_meta (
            key TEXT PRIMARY KEY,
            value TEXT
        );
    )";

    if (!executeQuery(createMetaTableSQL)) {
        std::println(std::cerr, "Failed to create mping_meta table");
        return false;
    }

    // 迁移旧 TIMESTAMP 列为 TIMESTAMPTZ
    if (!SchemaMigrator::migrateSchema(*this)) {
        std::println(std::cerr, "Warning: Schema migration incomplete, continuing anyway");
    }

    return true;
}

bool DatabaseManagerPG::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!connectSession()) {
        return false;
    }
    if (!prepareSchema()) {
        return false;
    }
    // 预建今天起未来分区（UTC 日界），避免插入时逐行触发 23514 补建
    if (!SchemaMigrator::
            ensurePingResultsPartitions(*this, ConfigDefaults::PING_PARTITION_LOOKAHEAD_DAYS)) {
        std::println(std::cerr, "Failed to create ping_results daily partitions");
        return false;
    }
    return true;
}

// 查询命令无需预建分区（不写入）；插入路径的 23514 补建逻辑兜底
bool DatabaseManagerPG::initializeForQuery() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!connectSession()) {
        return false;
    }
    return prepareSchema();
}

// 辅助函数：批量插入主机信息
// 单事务内参数化插入，防止 SQL 注入；任一失败回滚并返回 false。
// 先试单条多行 VALUES（一次往返）；分区表遇缺失分区（SQLSTATE 23514）
// 时回退到逐行 SAVEPOINT 插入（insertRowsWithSavepoints），补建目标日分区后重试该行
bool DatabaseManagerPG::insertBatch(
    const char* sqlPrefix, const char* rowTemplate, const char* sqlSuffix,
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& rows,
    const std::function<void(const std::tuple<std::string, std::string, short, bool, std::string>&,
                             std::vector<std::string>&)>& serialize,
    bool partitionedInsert) {
    if (rows.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(dbMutex);

    // 开始事务以提高性能
    if (!executeQuery("BEGIN;")) {
        std::println(std::cerr, "Failed to begin transaction for batch insert");
        return false;
    }

    // 一次性序列化全部行（批量与逐行回退共用）
    std::vector<std::vector<std::string>> allParams;
    allParams.reserve(rows.size());
    for (const auto& row : rows) {
        std::vector<std::string> params;
        serialize(row, params);
        allParams.push_back(std::move(params));
    }
    const size_t nParamsPerRow = allParams.front().size();

    // 行模板占位符重编号：$N → $(offset+N)；模板中可含字面量（如 NOW()、::integer）
    const auto applyOffset = [](const std::string& tpl, size_t offset) {
        std::string out;
        out.reserve(tpl.size() + 8);
        for (size_t i = 0; i < tpl.size(); i++) {
            if (tpl[i] != '$') {
                out += tpl[i];
                continue;
            }
            size_t j = i + 1;
            size_t n = 0;
            while (j < tpl.size() && std::isdigit(static_cast<unsigned char>(tpl[j]))) {
                n = n * 10 + static_cast<size_t>(tpl[j] - '0');
                j++;
            }
            out += "$" + std::to_string(n + offset);
            i = j - 1;
        }
        return out;
    };

    // ─── 批量路径：单条多行 VALUES 语句（一次往返）───
    const std::string tpl(rowTemplate);
    std::string all;
    all.reserve(allParams.size() * (tpl.size() + 8));
    for (size_t r = 0; r < allParams.size(); r++) {
        if (r > 0) {
            all += ",";
        }
        all += applyOffset(tpl, r * nParamsPerRow);
    }
    const std::string bulkSQL = std::string(sqlPrefix) + all + sqlSuffix;

    std::vector<std::string> flat;
    flat.reserve(allParams.size() * nParamsPerRow);
    for (const auto& rowParams : allParams) {
        flat.insert(flat.end(), rowParams.begin(), rowParams.end());
    }

    PGresult* res = execParams(bulkSQL, flat);
    if (res != nullptr && PQresultStatus(res) == PGRES_COMMAND_OK) {
        PQclear(res);
        if (!executeQuery("COMMIT;")) {
            std::println(std::cerr, "Failed to commit batch insert");
            return false;
        }
        return true;
    }

    // 分区缺失（23514）→ 回退逐行插入；其余错误直接失败
    const char* sqlstate = res != nullptr ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
    const bool needsFallback =
        partitionedInsert && sqlstate != nullptr && std::strcmp(sqlstate, "23514") == 0;
    const std::string bulkError = res != nullptr ? PQresultErrorMessage(res) : "out of memory";
    if (res != nullptr) {
        PQclear(res);
    }
    if (!needsFallback) {
        std::println(std::cerr, "Failed to insert batch: {}", bulkError);
        executeQuery("ROLLBACK;");
        return false;
    }

    // ─── 回退路径：逐行 SAVEPOINT 插入（分区补建后重试同一行）───
    const std::string rowSQL = std::string(sqlPrefix) + tpl + sqlSuffix;
    if (!insertRowsWithSavepoints(rowSQL, rows, allParams)) {
        return false;  // 已回滚
    }

    // 提交事务
    if (!executeQuery("COMMIT;")) {
        std::println(std::cerr, "Failed to commit batch insert");
        return false;
    }

    return true;
}

// 逐行 SAVEPOINT 插入：目标日分区缺失（SQLSTATE 23514）时补建分区并重试该行（最多一次）；
// 任何失败都已回滚事务并返回 false
bool DatabaseManagerPG::insertRowsWithSavepoints(
    const std::string& rowSQL,
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& rows,
    const std::vector<std::vector<std::string>>& allParams) {
    for (size_t i = 0; i < rows.size(); i++) {
        bool inserted = false;
        bool retried  = false;
        while (!inserted) {
            if (!executeQuery("SAVEPOINT sp_ping_row;")) {
                std::println(std::cerr, "Failed to create savepoint for batch insert");
                executeQuery("ROLLBACK;");
                return false;
            }
            PGresult* res = execParams(rowSQL, allParams[i]);
            if (res != nullptr && PQresultStatus(res) == PGRES_COMMAND_OK) {
                inserted = true;
                PQclear(res);
                executeQuery("RELEASE SAVEPOINT sp_ping_row;");
                break;
            }
            const char* sqlstate =
                res != nullptr ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
            if (!retried && sqlstate != nullptr && std::strcmp(sqlstate, "23514") == 0) {
                // 先回滚到保存点撤销失败语句对事务的污染，再补建分区并重试同一行
                executeQuery("ROLLBACK TO SAVEPOINT sp_ping_row;");
                executeQuery("RELEASE SAVEPOINT sp_ping_row;");
                if (res != nullptr) {
                    PQclear(res);
                }
                retried = true;
                if (!SchemaMigrator::createPartitionForTimestamp(*this, std::get<4>(rows[i]))) {
                    std::println(std::cerr, "Failed to create partition for record of IP {}",
                                 std::get<0>(rows[i]));
                    executeQuery("ROLLBACK;");
                    return false;
                }
                continue;
            }
            std::println(std::cerr, "Failed to insert record for IP {}: {}", std::get<0>(rows[i]),
                         res != nullptr ? PQresultErrorMessage(res) : "out of memory");
            if (res != nullptr) {
                PQclear(res);
            }
            executeQuery("ROLLBACK;");
            return false;
        }
    }
    return true;
}

// 批量插入或更新主机信息（UPSERT）
bool DatabaseManagerPG::insertHostsBatch(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    // 前缀为 INSERT 头部（不含 VALUES），模板含行内字面量（NOW()、::cast），
    // 后缀跟在行模板之后（由 insertBatch 拼接；占位符只在行模板中出现）
    const char* sqlPrefix =
        "INSERT INTO hosts (ip, hostname, last_seen, last_status, last_delay) VALUES";
    const char* rowTemplate = "($1, $2, NOW(), $4::boolean, $3::integer)";
    const char* sqlSuffix =
        " ON CONFLICT (ip) DO UPDATE SET "
        "hostname = EXCLUDED.hostname, "
        "last_seen = EXCLUDED.last_seen, "
        "last_status = EXCLUDED.last_status, "
        "last_delay = EXCLUDED.last_delay";
    return insertBatch(
        sqlPrefix, rowTemplate, sqlSuffix, results,
        [](const auto& row, std::vector<std::string>& params) {
            params = {std::get<0>(row), std::get<1>(row), std::to_string(std::get<2>(row)),
                      std::get<3>(row) ? "true" : "false"};
        },
        /*partitionedInsert=*/false);
}

// 批量插入ping结果
bool DatabaseManagerPG::insertPingResultsBatch(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    const char* sqlPrefix =
        "INSERT INTO ping_results (ip, hostname, delay, success, timestamp) VALUES";
    const char* rowTemplate = "($1, $2, $3::integer, $4::boolean, $5)";
    return insertBatch(
        sqlPrefix, rowTemplate, "", results,
        [](const auto& row, std::vector<std::string>& params) {
            params = {std::get<0>(row), std::get<1>(row), std::to_string(std::get<2>(row)),
                      std::get<3>(row) ? "true" : "false", std::get<4>(row)};
        },
        /*partitionedInsert=*/true);
}

bool DatabaseManagerPG::insertPingResults(
    const std::vector<std::tuple<std::string, std::string, short, bool, std::string>>& results) {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    if (results.empty()) {
        return true;  // 没有结果需要插入，视为成功
    }

    bool success = true;

    // 验证所有IP地址格式
    if (success) {
        for (const auto& result : results) {
            if (!IPValidator::isValidIPv4(std::get<0>(result))) {
                std::println(std::cerr, "Invalid IP address format: {}", std::get<0>(result));
                success = false;
                break;
            }
        }
    }

    // 在hosts表中批量插入或更新IP与主机名的映射关系
    if (success) {
        success = insertHostsBatch(results);
    }

    // 批量插入ping结果
    if (success) {
        success = insertPingResultsBatch(results);
    }

    return success;
}

void DatabaseManagerPG::queryIPStatistics(const std::string& ip) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }
    printHeader(ip, queryHostName(ip));

    auto stats = queryStatistics(ip);
    printBody(stats);

    if (stats.totalRecords > 0) {
        printRecentRecords(ip);
    }
}

std::string DatabaseManagerPG::queryHostName(const std::string& ip) {
    PGresult* res = execParams("SELECT hostname FROM hosts WHERE ip = $1", {ip});
    if (res == nullptr || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return {};
    }

    std::string hostname;
    if (PQntuples(res) > 0 && PQgetvalue(res, 0, 0)) {
        hostname = PQgetvalue(res, 0, 0);
    }
    PQclear(res);
    return hostname;
}

PingStatistics DatabaseManagerPG::queryStatistics(const std::string& ip) {
    PingStatistics stats;
    PGresult* res = execParams(
        "SELECT COUNT(*), COUNT(*) FILTER (WHERE success = true), "
        "AVG(delay) FILTER (WHERE success = true), "
        "MAX(delay) FILTER (WHERE success = true), "
        "MIN(delay) FILTER (WHERE success = true) "
        "FROM ping_results WHERE ip = $1",
        {ip});
    if (res == nullptr || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return stats;
    }

    if (PQntuples(res) > 0) {
        stats.totalRecords = std::atoi(PQgetvalue(res, 0, 0));
        stats.successCount = std::atoi(PQgetvalue(res, 0, 1));
        stats.avgDelay     = PQgetisnull(res, 0, 2) ? 0.0 : std::atof(PQgetvalue(res, 0, 2));
        stats.maxDelay     = PQgetisnull(res, 0, 3) ? 0 : std::atoi(PQgetvalue(res, 0, 3));
        stats.minDelay     = PQgetisnull(res, 0, 4) ? 0 : std::atoi(PQgetvalue(res, 0, 4));
        stats.failureCount = stats.totalRecords - stats.successCount;
        if (stats.totalRecords > 0) {
            stats.successRate = (double)stats.successCount / stats.totalRecords * 100;
            stats.failureRate = (double)stats.failureCount / stats.totalRecords * 100;
        }
    }
    PQclear(res);
    return stats;
}

void DatabaseManagerPG::printRecentRecords(const std::string& ip) {
    PGresult* res = execParams(
        "SELECT delay, success, timestamp FROM ping_results WHERE ip = $1 "
        "ORDER BY timestamp DESC LIMIT 10",
        {ip});
    if (res == nullptr || PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to query recent records");
        if (res) PQclear(res);
        return;
    }

    printRecentRecordsHeader();
    for (int i = 0; i < PQntuples(res); i++) {
        char* ts = PQgetvalue(res, i, 2);
        char* d  = PQgetvalue(res, i, 0);
        char* s  = PQgetvalue(res, i, 1);
        printRecentRecordRow(ts ? ts : "N/A", d ? std::atoi(d) : 0, s && std::strcmp(s, "t") == 0);
    }
    PQclear(res);
}

// 删除 ping_results 表中超过 days 天的旧记录；返回删除行数，失败返回 -1。
// 按日分区表：整日早于截止期的分区直接 DROP（瞬时释放磁盘空间），
// 边界日内更早的行仍用 DELETE 精确删除
int DatabaseManagerPG::deleteOldPingResults(int days) {
    // 1. 列出整日已过期的分区（分区名 ping_results_YYYYMMDD，UTC 日界）
    PGresult* res = execParams(
        "SELECT tablename FROM pg_tables "
        "WHERE schemaname = 'public' AND tablename ~ '^ping_results_[0-9]{8}$' "
        "AND to_timestamp(substring(tablename FROM 14), 'YYYYMMDD') + INTERVAL '1 day' "
        "<= NOW() - ($1 * INTERVAL '1 day') ORDER BY tablename",
        {std::to_string(days)});
    if (res == nullptr || PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to list old ping_results partitions: {}",
                     res ? PQresultErrorMessage(res) : "out of memory");
        if (res) PQclear(res);
        return -1;
    }

    int dropped = 0;
    for (int i = 0; i < PQntuples(res); i++) {
        const std::string name = PQgetvalue(res, i, 0);
        if (!executeQuery("DROP TABLE " + name)) {
            PQclear(res);
            return -1;
        }
        dropped++;
    }
    PQclear(res);
    if (dropped > 0) {
        std::println(std::cout, "Dropped {} day partition(s) from ping_results", dropped);
    }

    // 2. 剩余过期行（当前日分区内）逐行删除，保证清理边界精确
    res = execParams("DELETE FROM ping_results WHERE timestamp < NOW() - ($1 * INTERVAL '1 day')",
                     {std::to_string(days)});
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to delete old ping results: {}",
                     res ? PQresultErrorMessage(res) : "out of memory");
        if (res) PQclear(res);
        return -1;
    }

    int deleted = std::atoi(PQcmdTuples(res));
    PQclear(res);
    return deleted;
}

void DatabaseManagerPG::cleanupOldData(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    std::println(std::cout, "Cleaning up data older than {} days...", days);

    int totalDeleted = deleteOldPingResults(days);
    if (totalDeleted < 0) {
        return;
    }
    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);

    // 清理alerts表中超过指定天数的告警记录
    PGresult* cleanupAlertsRes =
        execParams("DELETE FROM alerts WHERE created_time < NOW() - ($1 * INTERVAL '1 day')",
                   {std::to_string(days)});

    if (PQresultStatus(cleanupAlertsRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to cleanup old alerts: {}",
                     PQresultErrorMessage(cleanupAlertsRes));
        PQclear(cleanupAlertsRes);
        return;
    }

    int alertsDeleted = std::atoi(PQcmdTuples(cleanupAlertsRes));
    PQclear(cleanupAlertsRes);

    if (alertsDeleted > 0) {
        std::println(std::cout, "Deleted {} old alert records", alertsDeleted);
    }

    // 清理recovery_records表中超过指定天数的恢复记录
    PGresult* cleanupRecoveryRes = execParams(
        "DELETE FROM recovery_records WHERE recovery_time < NOW() - ($1 * INTERVAL '1 day')",
        {std::to_string(days)});

    if (PQresultStatus(cleanupRecoveryRes) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to cleanup old recovery records: {}",
                     PQresultErrorMessage(cleanupRecoveryRes));
        PQclear(cleanupRecoveryRes);
        return;
    }

    int recoveryDeleted = std::atoi(PQcmdTuples(cleanupRecoveryRes));
    PQclear(cleanupRecoveryRes);

    if (recoveryDeleted > 0) {
        std::println(std::cout, "Deleted {} old recovery records", recoveryDeleted);
    }

    std::println(std::cout, "Cleanup completed.");
}

void DatabaseManagerPG::cleanupOldPingResults(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return;
    }

    // 节流：距上次自动清理不足 CLEANUP_MIN_INTERVAL_SECONDS 秒时跳过（无输出）。
    // ping 运行可能很频繁，分区 DROP/DELETE 是低频操作，避免每次运行多两次查询
    const std::string interval =
        std::to_string(ConfigDefaults::CLEANUP_MIN_INTERVAL_SECONDS) + " seconds";
    PGresult* dueRes = execParams(
        "SELECT COALESCE((SELECT value::timestamptz FROM mping_meta WHERE key = $1), 'epoch') "
        "<= NOW() - $2::interval",
        {ConfigDefaults::META_LAST_AUTO_PING_CLEANUP, interval});
    if (dueRes == nullptr || PQresultStatus(dueRes) != PGRES_TUPLES_OK) {
        std::println(std::cerr, "Failed to check cleanup throttle: {}",
                     dueRes ? PQresultErrorMessage(dueRes) : "out of memory");
        if (dueRes) PQclear(dueRes);
        return;
    }
    const bool due =
        PQgetisnull(dueRes, 0, 0) ? false : std::strcmp(PQgetvalue(dueRes, 0, 0), "t") == 0;
    PQclear(dueRes);
    if (!due) {
        return;
    }

    std::println(std::cout, "Cleaning up ping_results older than {} days...", days);

    int totalDeleted = deleteOldPingResults(days);
    if (totalDeleted < 0) {
        return;
    }
    std::println(std::cout, "Deleted {} old records from ping_results table", totalDeleted);

    // 记录本次自动清理时间（会话时区固定 UTC，NOW()::text 即为 UTC 时间戳）
    PGresult* mark = execParams(
        "INSERT INTO mping_meta (key, value) VALUES ($1, NOW()::text) "
        "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value",
        {ConfigDefaults::META_LAST_AUTO_PING_CLEANUP});
    if (mark == nullptr || PQresultStatus(mark) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to record cleanup time: {}",
                     mark ? PQresultErrorMessage(mark) : "out of memory");
        if (mark) PQclear(mark);
    } else {
        PQclear(mark);
    }
}

// 批量新增告警：复用 insertBatch 的批量路径（非分区表，单条多行 UPSERT）。
// IP 逐个校验，非法项打印警告并跳过；合法项仍批量写入（与原逐行失败不中断语义一致）
bool DatabaseManagerPG::addAlerts(const std::vector<std::tuple<std::string, std::string>>& alerts) {
    if (alerts.empty()) {
        return true;
    }

    bool allValid = true;
    std::vector<std::tuple<std::string, std::string, short, bool, std::string>> rows;
    rows.reserve(alerts.size());
    for (const auto& [ip, hostname] : alerts) {
        if (!IPValidator::isValidIPv4(ip)) {
            std::println(std::cerr, "Invalid IP address format: {}", ip);
            allValid = false;
            continue;
        }
        rows.emplace_back(ip, hostname, 0, false, "");
    }
    if (rows.empty()) {
        return false;  // 全部非法（调用方会看到失败并打印提示）
    }

    return insertBatch(
               "INSERT INTO alerts (ip, hostname, created_time) VALUES", "($1, $2, NOW())",
               " ON CONFLICT (ip) DO NOTHING", rows,
               [](const auto& row, std::vector<std::string>& params) {
                   params = {std::get<0>(row), std::get<1>(row)};
               },
               /*partitionedInsert=*/false)
           && allValid;
}

std::map<std::string, std::string> DatabaseManagerPG::getAllHosts() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::map<std::string, std::string> hosts;

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return hosts;
    }

    // 查询所有主机，按IP排序以提高查询效率
    PGresult* res = executeQueryWithResult("SELECT ip, hostname FROM hosts ORDER BY ip;");
    if (!res) {
        std::println(std::cerr, "Failed to query hosts");
        return hosts;
    }

    for (int row = 0; row < PQntuples(res); row++) {
        char* ip       = PQgetvalue(res, row, 0);
        char* hostname = PQgetvalue(res, row, 1);

        if (ip) {
            std::string ipStr       = ip;
            std::string hostnameStr = hostname ? hostname : "";
            hosts[ipStr]            = hostnameStr;
        }
    }

    PQclear(res);
    return hosts;
}

bool DatabaseManagerPG::addAlert(const std::string& ip, const std::string& hostname) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 验证IP地址格式
    if (!IPValidator::isValidIPv4(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 使用参数化查询插入或更新告警记录
    PGresult* res = execParams(
        "INSERT INTO alerts (ip, hostname, created_time) "
        "VALUES ($1, $2, NOW()) "
        "ON CONFLICT (ip) DO NOTHING",
        {ip, hostname});

    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to add alert for IP {}: {}", ip,
                     res ? PQresultErrorMessage(res) : "Unknown error");
        if (res) PQclear(res);
        return false;
    }

    PQclear(res);
    return true;
}

bool DatabaseManagerPG::removeAlert(const std::string& ip) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return false;
    }

    // 验证IP地址格式
    if (!IPValidator::isValidIPv4(ip)) {
        std::println(std::cerr, "Invalid IP address format: {}", ip);
        return false;
    }

    // 单条语句原子完成：删除告警并把被删告警写入恢复记录。
    // 告警不存在时无行删除也不产生恢复记录，且中途失败不会丢失恢复数据。
    PGresult* res = execParams(
        "WITH gone AS (DELETE FROM alerts WHERE ip = $1 RETURNING hostname, created_time) "
        "INSERT INTO recovery_records (ip, hostname, alert_time, recovery_time) "
        "SELECT $1, hostname, created_time, NOW() FROM gone",
        {ip});
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::println(std::cerr, "Failed to remove alert for IP {}: {}", ip,
                     res ? PQresultErrorMessage(res) : "Unknown error");
        if (res) PQclear(res);
        return false;
    }
    PQclear(res);

    return true;
}

// 查询执行器：days >= 0 时用 $1 参数化过滤，否则执行全部记录查询；连接未就绪返回 nullptr
PGresult* DatabaseManagerPG::executeOptionalDays(const char* sqlDays, const char* sqlAll,
                                                 int days) {
    if (!conn) {
        std::println(std::cerr, "Database not initialized");
        return nullptr;
    }

    if (days >= 0) {
        // 参数化查询：只取指定天数内的记录
        return execParams(sqlDays, {std::to_string(days)});
    }
    return PQexec(conn.get(), sqlAll);
}

std::vector<std::tuple<std::string, std::string, std::string>> DatabaseManagerPG::getActiveAlerts(
    int days) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<std::string, std::string, std::string>> alerts;

    // 查询活动告警，按创建时间排序
    const char* sqlDays =
        "SELECT ip, hostname, created_time FROM alerts WHERE created_time >= NOW() - ($1 * "
        "INTERVAL '1 day') ORDER BY created_time DESC";
    const char* sqlAll = "SELECT ip, hostname, created_time FROM alerts ORDER BY created_time DESC";
    PGresult* res      = executeOptionalDays(sqlDays, sqlAll, days);
    if (!res
        || (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK)) {
        std::println(std::cerr, "Failed to query alerts");
        if (res) PQclear(res);
        return alerts;
    }

    // 预分配空间以提高性能
    alerts.reserve(PQntuples(res));
    for (int row = 0; row < PQntuples(res); row++) {
        char* ip           = PQgetvalue(res, row, 0);
        char* hostname     = PQgetvalue(res, row, 1);
        char* created_time = PQgetvalue(res, row, 2);

        alerts.emplace_back(ip ? ip : "", hostname ? hostname : "",
                            created_time ? created_time : "");
    }

    PQclear(res);
    return alerts;
}

std::vector<std::tuple<int, std::string, std::string, std::string, std::string>>
DatabaseManagerPG::getRecoveryRecords(int days) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> records;

    // 查询恢复记录，按恢复时间排序
    const char* sqlDays =
        "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records WHERE "
        "recovery_time >= NOW() - ($1 * INTERVAL '1 day') ORDER BY recovery_time DESC";
    const char* sqlAll =
        "SELECT id, ip, hostname, alert_time, recovery_time FROM recovery_records ORDER BY "
        "recovery_time DESC";
    PGresult* res = executeOptionalDays(sqlDays, sqlAll, days);
    if (!res
        || (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK)) {
        std::println(std::cerr, "Failed to query recovery records");
        if (res) PQclear(res);
        return records;
    }

    // 预分配空间以提高性能
    records.reserve(PQntuples(res));
    for (int row = 0; row < PQntuples(res); row++) {
        int id              = std::atoi(PQgetvalue(res, row, 0));
        char* ip            = PQgetvalue(res, row, 1);
        char* hostname      = PQgetvalue(res, row, 2);
        char* alert_time    = PQgetvalue(res, row, 3);
        char* recovery_time = PQgetvalue(res, row, 4);

        std::string ipStr           = ip ? ip : "";
        std::string hostnameStr     = hostname ? hostname : "";
        std::string alertTimeStr    = alert_time ? alert_time : "";
        std::string recoveryTimeStr = recovery_time ? recovery_time : "";

        records.emplace_back(id, ipStr, hostnameStr, alertTimeStr, recoveryTimeStr);
    }

    PQclear(res);
    return records;
}
