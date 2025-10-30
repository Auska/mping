cmake_minimum_required(VERSION 3.10)
project(mping VERSION 1.1.0)

# 定义项目信息
set(PROJECT_DESCRIPTION "Multi-host Ping Tool")
set(PROJECT_HOMEPAGE_URL "https://github.com/Auska/mping")

# 设置C++标准为C++23以获得更好的性能和现代特性
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置默认构建类型为Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 启用优化标志
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# 设置编译时间
string(TIMESTAMP COMPILE_TIME "%Y-%m-%d %H:%M:%S")

# Set default installation prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation directory" FORCE)
endif()

# Configure version header file
configure_file(project_info.h.in project_info.h @ONLY)

# 选项：是否启用PostgreSQL支持
option(USE_POSTGRESQL "Use PostgreSQL database" OFF)

# 选项：是否编译测试程序
option(BUILD_TESTS "Build test programs" OFF)

# Find SQLite3 library
find_package(SQLite3 REQUIRED)
if(USE_POSTGRESQL)
    find_package(PkgConfig REQUIRED)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PQ REQUIRED libpq)
    endif()
endif()

# Add executable
if(USE_POSTGRESQL)
    add_executable(mping main.cpp database_manager.cpp database_manager_pg.cpp ping_manager.cpp config_manager.cpp utils.cpp version_info.cpp)
else()
    add_executable(mping main.cpp database_manager.cpp ping_manager.cpp config_manager.cpp utils.cpp version_info.cpp)
endif()

# Add test executables (only when explicitly requested)
if(BUILD_TESTS)
    add_executable(test_sqlite_alerts test_sqlite_alerts.cpp database_manager.cpp utils.cpp)
    target_link_libraries(test_sqlite_alerts PRIVATE Threads::Threads SQLite::SQLite3)
    
    add_executable(test_timezone test_timezone.cpp database_manager.cpp utils.cpp)
    target_link_libraries(test_timezone PRIVATE Threads::Threads SQLite::SQLite3)
    
    add_executable(test_alert_persistence test_alert_persistence.cpp database_manager.cpp utils.cpp)
    target_link_libraries(test_alert_persistence PRIVATE Threads::Threads SQLite::SQLite3)
    
    add_executable(test_recovery_records test_recovery_records.cpp database_manager.cpp utils.cpp)
    target_link_libraries(test_recovery_records PRIVATE Threads::Threads SQLite::SQLite3)
    
    add_executable(test_query_recovery test_query_recovery.cpp database_manager.cpp utils.cpp)
    target_link_libraries(test_query_recovery PRIVATE Threads::Threads SQLite::SQLite3)
    
    if(USE_POSTGRESQL)
        add_executable(test_pg test_pg.cpp database_manager_pg.cpp utils.cpp)
        target_link_libraries(test_pg PRIVATE Threads::Threads ${PQ_LDFLAGS})
        target_include_directories(test_pg PRIVATE ${PQ_INCLUDE_DIRS})
        target_compile_options(test_pg PRIVATE ${PQ_CFLAGS_OTHER})
    endif()
endif()

# Include the directory where project_info.h is generated
target_include_directories(mping PRIVATE ${CMAKE_BINARY_DIR})

# Add compile definitions for version information
target_compile_definitions(mping PRIVATE 
    PROJECT_NAME="${PROJECT_NAME}"
    PROJECT_VERSION="${PROJECT_VERSION}"
    PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    PROJECT_DESCRIPTION="${PROJECT_DESCRIPTION}"
    PROJECT_HOMEPAGE_URL="${PROJECT_HOMEPAGE_URL}"
    COMPILE_TIME="${COMPILE_TIME}"
)

# 如果启用PostgreSQL，添加相应的编译定义
if(USE_POSTGRESQL)
    target_compile_definitions(mping PRIVATE USE_POSTGRESQL)
endif()

# 设置优化标志
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mping PRIVATE -O3)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(mping PRIVATE -g -O0)
endif()

# Link libraries
find_package(Threads REQUIRED)
if(USE_POSTGRESQL)
    target_link_libraries(mping PRIVATE Threads::Threads SQLite::SQLite3 ${PQ_LDFLAGS})
    target_include_directories(mping PRIVATE ${PQ_INCLUDE_DIRS})
    target_compile_options(mping PRIVATE ${PQ_CFLAGS_OTHER})
    
    # Link libraries for test executables (only when explicitly requested)
    if(BUILD_TESTS)
        target_link_libraries(test_sqlite_alerts PRIVATE Threads::Threads SQLite::SQLite3)
        target_link_libraries(test_timezone PRIVATE Threads::Threads SQLite::SQLite3)
        target_link_libraries(test_alert_persistence PRIVATE Threads::Threads SQLite::SQLite3)
        target_link_libraries(test_pg PRIVATE Threads::Threads ${PQ_LDFLAGS})
        target_include_directories(test_pg PRIVATE ${PQ_INCLUDE_DIRS})
        target_compile_options(test_pg PRIVATE ${PQ_CFLAGS_OTHER})
    endif()
else()
    target_link_libraries(mping PRIVATE Threads::Threads SQLite::SQLite3)
    if(BUILD_TESTS)
        target_link_libraries(test_sqlite_alerts PRIVATE Threads::Threads SQLite::SQLite3)
        target_link_libraries(test_timezone PRIVATE Threads::Threads SQLite::SQLite3)
        target_link_libraries(test_alert_persistence PRIVATE Threads::Threads SQLite::SQLite3)
    endif()
endif()

# Install rules
install(TARGETS mping RUNTIME DESTINATION bin)

# 添加pkg-config文件支持
configure_file(mping.pc.in mping.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/mping.pc DESTINATION lib/pkgconfig)
