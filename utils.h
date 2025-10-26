#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// 从文件中读取主机列表
std::map<std::string, std::string> readHostsFromFile(const std::string& filename);

#endif // UTILS_H