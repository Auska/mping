#ifndef UTILS_H
#define UTILS_H

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

/**
 * @brief 从文件中读取主机列表
 * 
 * 从指定文件中读取 IP 地址和主机名列表，文件格式为每行一个 IP 地址和主机名，
 * 以空格或制表符分隔。以 # 开头的行被视为注释并忽略。
 * 
 * @param filename 要读取的文件名
 * 
 * @return std::map<std::string, std::string> 返回主机列表，格式为 {IP: hostname}
 * 
 * @throws std::invalid_argument 如果文件名为空
 * 
 * @note 如果文件无法打开或格式错误，会输出警告信息但不会抛出异常
 * @note IP 地址格式会被验证，无效的 IP 地址会被跳过
 * 
 * @code
 * auto hosts = readHostsFromFile("ip.txt");
 * for (const auto& [ip, hostname] : hosts) {
 *     std::cout << ip << " -> " << hostname << std::endl;
 * }
 * @endcode
 */
std::map<std::string, std::string> readHostsFromFile(const std::string& filename);

#endif  // UTILS_H