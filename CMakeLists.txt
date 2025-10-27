cmake_minimum_required(VERSION 3.10)
project(mping VERSION 1.0.0)

# 定义项目信息
set(PROJECT_DESCRIPTION "Multi-host Ping Tool")
set(PROJECT_HOMEPAGE_URL "https://github.com/Auska/mping")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default installation prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation directory" FORCE)
endif()

# Configure version header file
configure_file(project_info.h.in project_info.h @ONLY)

# Find SQLite3 library
find_package(SQLite3 REQUIRED)

# Add executable
add_executable(mping main.cpp database_manager.cpp ping_manager.cpp config_manager.cpp utils.cpp version_info.cpp)

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
)

# Link pthread and SQLite3 libraries
find_package(Threads REQUIRED)
target_link_libraries(mping PRIVATE Threads::Threads SQLite::SQLite3)

# Install rules
install(TARGETS mping RUNTIME DESTINATION bin)
