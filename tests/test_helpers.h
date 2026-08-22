#ifndef MPING_TEST_HELPERS_H
#define MPING_TEST_HELPERS_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// raw ICMP 需要 root/cap_net_raw 特权；缺失时跳过依赖真实可达性的断言
inline bool haveRawPingCapability() {
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

#endif  // MPING_TEST_HELPERS_H
