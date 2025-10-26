cmake_minimum_required(VERSION 3.10)
project(mping)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找SQLite3库
find_package(SQLite3 REQUIRED)

# 添加可执行文件
add_executable(mping main.cpp print_results.cpp db.cpp)

# 链接线程库和SQLite3库
find_package(Threads REQUIRED)
target_link_libraries(mping PRIVATE Threads::Threads SQLite::SQLite3)