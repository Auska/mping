#ifndef PING_H
#define PING_H

#include <string>
#include <map>
#include <vector>
#include <tuple>
#include <future>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "db.h"

// 执行ping操作并返回结果
std::vector<std::tuple<std::string, std::string, bool, long long, std::string>> performPing(const std::map<std::string, std::string>& hosts);

#endif // PING_H