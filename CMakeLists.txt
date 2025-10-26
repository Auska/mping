cmake_minimum_required(VERSION 3.10)
project(mping)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default installation prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation directory" FORCE)
endif()

# Find SQLite3 library
find_package(SQLite3 REQUIRED)

# Add executable
add_executable(mping main.cpp database_manager.cpp ping_manager.cpp config_manager.cpp utils.cpp)

# Link pthread and SQLite3 libraries
find_package(Threads REQUIRED)
target_link_libraries(mping PRIVATE Threads::Threads SQLite::SQLite3)

# Install rules
install(TARGETS mping RUNTIME DESTINATION bin)