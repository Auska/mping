cmake_minimum_required(VERSION 3.10)
project(mping)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加可执行文件
add_executable(mping main.cpp print_results.cpp)

# 链接线程库
find_package(Threads REQUIRED)
target_link_libraries(mping PRIVATE Threads::Threads)

# 启用文件系统库（如果需要）
# target_link_libraries(mping PRIVATE stdc++fs)