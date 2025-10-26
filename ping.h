#ifndef PING_H
#define PING_H

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <future>
#include <chrono>
#include <sstream>
#include <iomanip>

std::vector<std::tuple<std::string, std::string, bool, short, std::string>> performPing(const std::map<std::string, std::string>& hosts);

#endif // PING_H