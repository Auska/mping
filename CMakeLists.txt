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

# 选项：选择数据库后端 (SQLite 或 PostgreSQL)
option(USE_POSTGRESQL "Use PostgreSQL database (default is SQLite)" OFF)

# 选项：是否编译测试程序
option(BUILD_TESTS "Build test programs" OFF)

# Find SQLite3 library only if not using PostgreSQL
if(NOT USE_POSTGRESQL)
    find_package(SQLite3 REQUIRED)
endif()

# 根据选项查找PostgreSQL或SQLite
if(USE_POSTGRESQL)
    find_package(PkgConfig REQUIRED)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PQ REQUIRED libpq)
    endif()
endif()

# Add executable - include only the necessary database manager
if(USE_POSTGRESQL)
    add_executable(mping main.cpp database_manager_pg.cpp database_factory.cpp ping_manager.cpp config_manager.cpp utils.cpp version_info.cpp)
    target_compile_definitions(mping PRIVATE USE_POSTGRESQL=1)
else()
    add_executable(mping main.cpp database_manager.cpp database_factory.cpp ping_manager.cpp config_manager.cpp utils.cpp version_info.cpp)
    target_compile_definitions(mping PRIVATE USE_SQLITE=1)
endif()

# Add test executables (only when explicitly requested)
if(BUILD_TESTS)
    # Find Catch2 test framework
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        # If Catch2 is not found, download it
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.0
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    # Add test executable with Catch2
    add_executable(mping_tests
        tests/test_main.cpp
        tests/test_database_manager.cpp
        tests/test_ping_manager.cpp
        tests/test_utils.cpp
    )
    
    target_link_libraries(mping_tests PRIVATE Catch2::Catch2WithMain)
    target_include_directories(mping_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
    
    if(NOT USE_POSTGRESQL)
        find_package(SQLite3 REQUIRED)
        target_sources(mping_tests PRIVATE
            database_manager.cpp
            database_factory.cpp
            ping_manager.cpp
            config_manager.cpp
            utils.cpp
            version_info.cpp
        )
        target_link_libraries(mping_tests PRIVATE Threads::Threads SQLite::SQLite3)
    else()
        target_sources(mping_tests PRIVATE
            database_manager_pg.cpp
            database_factory.cpp
            ping_manager.cpp
            config_manager.cpp
            utils.cpp
            version_info.cpp
        )
        target_link_libraries(mping_tests PRIVATE Threads::Threads ${PQ_LDFLAGS})
        target_include_directories(mping_tests PRIVATE ${PQ_INCLUDE_DIRS})
        target_compile_options(mping_tests PRIVATE ${PQ_CFLAGS_OTHER})
    endif()
    
    target_include_directories(mping_tests PRIVATE ${CMAKE_BINARY_DIR})
    
    # Register tests with CTest
    include(CTest)
    include(Catch)
    catch_discover_tests(mping_tests)

    # Legacy test executables (for backward compatibility, SQLite only)
    if(NOT USE_POSTGRESQL)
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
    endif()
    
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

# 添加数据库后端编译定义
if(USE_POSTGRESQL)
    target_compile_definitions(mping PRIVATE USE_POSTGRESQL=1)
else()
    target_compile_definitions(mping PRIVATE USE_SQLITE=1)
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
    target_link_libraries(mping PRIVATE Threads::Threads ${PQ_LDFLAGS})
    target_include_directories(mping PRIVATE ${PQ_INCLUDE_DIRS})
    target_compile_options(mping PRIVATE ${PQ_CFLAGS_OTHER})
    
    # Link libraries for test executables (only when explicitly requested)
    if(BUILD_TESTS)
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
        target_link_libraries(test_recovery_records PRIVATE Threads::Threads SQLite::SQLite3)
        target_link_libraries(test_query_recovery PRIVATE Threads::Threads SQLite::SQLite3)
    endif()
endif()

# Install rules
install(TARGETS mping RUNTIME DESTINATION bin)

# 添加pkg-config文件支持
configure_file(mping.pc.in mping.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/mping.pc DESTINATION lib/pkgconfig)
