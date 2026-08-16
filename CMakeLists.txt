cmake_minimum_required(VERSION 3.16)

project(mping
    VERSION 1.1.0
    DESCRIPTION "Multi-host Ping Tool"
    HOMEPAGE_URL "https://github.com/Auska/mping"
    LANGUAGES CXX)

# Windows is not supported
if(WIN32)
    message(FATAL_ERROR "mping does not support Windows: POSIX (Linux/macOS/BSD) required")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release for a production build
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(MPING_USE_POSTGRESQL "Use PostgreSQL database backend (default is SQLite)" OFF)
option(MPING_BUILD_TESTS "Build the mping test programs" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

# UTC compile timestamp, injected as a macro (see src/version_info.cpp)
string(TIMESTAMP MPING_COMPILE_TIME "%Y-%m-%d %H:%M:%S" UTC)

find_package(Threads REQUIRED)

# SQLite is the default backend and is always used by the test suite
find_package(SQLite3 REQUIRED)

set(MPING_COMMON_SOURCES
    src/commands.cpp
    src/database_factory.cpp
    src/ping_manager.cpp
    src/config_manager.cpp
    src/config_file.cpp
    src/version_info.cpp
    src/utils.cpp)

# Version/description macros mirrored from project() metadata
set(MPING_COMMON_DEFINES
    "PROJECT_NAME=\"mping\""
    "PROJECT_VERSION=\"${PROJECT_VERSION}\""
    "PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
    "PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
    "PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
    "PROJECT_DESCRIPTION=\"${PROJECT_DESCRIPTION}\""
    "PROJECT_HOMEPAGE_URL=\"${PROJECT_HOMEPAGE_URL}\""
    "COMPILE_TIME=\"${MPING_COMPILE_TIME}\"")

add_executable(mping
    src/main.cpp
    ${MPING_COMMON_SOURCES})
target_include_directories(mping PRIVATE src include)
target_compile_definitions(mping PRIVATE ${MPING_COMMON_DEFINES})
target_link_libraries(mping PRIVATE Threads::Threads)

if(MPING_USE_POSTGRESQL)
    find_package(PostgreSQL REQUIRED)
    target_sources(mping PRIVATE src/database_manager_pg.cpp)
    target_compile_definitions(mping PRIVATE USE_POSTGRESQL=1)
    target_link_libraries(mping PRIVATE PostgreSQL::PostgreSQL)
else()
    target_sources(mping PRIVATE src/database_manager.cpp)
    target_compile_definitions(mping PRIVATE USE_SQLITE=1)
    target_link_libraries(mping PRIVATE SQLite::SQLite3)
endif()

install(TARGETS mping RUNTIME DESTINATION bin)

if(MPING_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
    add_executable(mping_tests
        ${MPING_COMMON_SOURCES}
        tests/test_main.cpp
        tests/test_commands.cpp
        tests/test_database_manager.cpp
        tests/test_ping_manager.cpp
        tests/test_utils.cpp
        tests/test_config_manager.cpp
        tests/test_version_info.cpp
        tests/test_config_file.cpp)
    target_include_directories(mping_tests PRIVATE src include)
    target_compile_definitions(mping_tests PRIVATE ${MPING_COMMON_DEFINES})
    target_link_libraries(mping_tests PRIVATE Threads::Threads Catch2::Catch2)
    if(Catch2_VERSION VERSION_GREATER_EQUAL "3.7.0")
        # Catch2 >= 3.7 removed the header-only CATCH_CONFIG_MAIN main()
        # generator; the executable entry point now comes from
        # Catch2::Catch2WithMain (tests/test_main.cpp keeps the define as a no-op).
        target_link_libraries(mping_tests PRIVATE Catch2::Catch2WithMain)
    endif()

    if(MPING_USE_POSTGRESQL)
        # Tests exercise both backends, mirroring the previous dual-DB test build
        target_sources(mping_tests PRIVATE src/database_manager_pg.cpp src/database_manager.cpp)
        target_compile_definitions(mping_tests PRIVATE USE_POSTGRESQL=1 USE_SQLITE=1)
        target_link_libraries(mping_tests PRIVATE PostgreSQL::PostgreSQL SQLite::SQLite3)
    else()
        target_sources(mping_tests PRIVATE src/database_manager.cpp)
        target_compile_definitions(mping_tests PRIVATE USE_SQLITE=1)
        target_link_libraries(mping_tests PRIVATE SQLite::SQLite3)
    endif()

    include(CTest)
    add_test(NAME mping_tests COMMAND mping_tests)
endif()
