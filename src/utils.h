#ifndef UTILS_H
#define UTILS_H

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

// 从文件中读取主机列表
std::map<std::string, std::string> readHostsFromFile(const std::string& filename);

#endif  // UTILS_H