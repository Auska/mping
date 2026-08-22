#ifndef IP_VALIDATOR_H
#define IP_VALIDATOR_H

#include <arpa/inet.h>

#include <string>

/**
 * @brief IP 地址验证工具
 *
 * 提供统一的 IPv4 地址验证功能，避免代码重复。
 * 直接用系统 inet_pton 做严格解析，取代手写正则（无正则库初始化开销）。
 * 注意：inet_pton 拒绝前导零（如 "010.0.0.1"），比旧正则更严格。
 */
namespace IPValidator {

/**
 * @brief 验证 IPv4 地址格式
 *
 * @param ip 要验证的 IP 地址字符串
 * @return true 如果是有效的 IPv4 地址
 * @return false 如果不是有效的 IPv4 地址
 */
inline bool isValidIPv4(const std::string& ip) {
    struct in_addr addr{};
    return inet_pton(AF_INET, ip.c_str(), &addr) == 1;
}

}  // namespace IPValidator

#endif  // IP_VALIDATOR_H
