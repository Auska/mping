#ifndef IP_VALIDATOR_H
#define IP_VALIDATOR_H

#include <regex>
#include <string>

/**
 * @brief IP 地址验证工具
 *
 * 提供统一的 IPv4 地址验证功能，避免代码重复。
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
    static const std::regex ipPattern(
        "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
        "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    return std::regex_match(ip, ipPattern);
}

}  // namespace IPValidator

#endif  // IP_VALIDATOR_H
