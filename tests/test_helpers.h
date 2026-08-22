#ifndef MPING_TEST_HELPERS_H
#define MPING_TEST_HELPERS_H

#include <libpq-fe.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

// raw ICMP 需要 root/cap_net_raw 特权；缺失时跳过依赖真实可达性的断言
inline bool haveRawPingCapability() {
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

// 数据库相关测试使用的 PostgreSQL 连接串：
// 环境变量 MPING_TEST_PG_CONNSTR 覆盖默认值，例如
//   MPING_TEST_PG_CONNSTR='host=localhost user=postgres dbname=mping_test'
inline std::string testPgConnstr() {
    const char* env = std::getenv("MPING_TEST_PG_CONNSTR");
    return env ? env : "host=localhost user=postgres dbname=postgres connect_timeout=3";
}

// 静默探测 PostgreSQL 是否可达（不建表不发查询），不可用返回 false
inline bool pgAvailable() {
    static const bool ok = [] {
        PGconn* conn         = PQconnectdb(testPgConnstr().c_str());
        const bool connected = PQstatus(conn) == CONNECTION_OK;
        PQfinish(conn);
        return connected;
    }();
    return ok;
}

#endif  // MPING_TEST_HELPERS_H
