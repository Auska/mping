#include "version_info.h"
#include <iostream>
#include <string>

void print_version_info() {
    std::cout << PROJECT_NAME << " version " << PROJECT_VERSION << std::endl;
    std::cout << "Description: " << PROJECT_DESCRIPTION << std::endl;
    std::cout << "Homepage: " << PROJECT_HOMEPAGE_URL << std::endl;
    std::cout << "Version: " << PROJECT_VERSION_MAJOR << "." << PROJECT_VERSION_MINOR << "." << PROJECT_VERSION_PATCH << std::endl;
    std::cout << "Build Time: " << COMPILE_TIME << std::endl;
    
    // 显示编译器信息
#ifdef __GNUC__
    std::cout << "Compiler: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << std::endl;
#elif defined(_MSC_VER)
    std::cout << "Compiler: MSVC " << _MSC_VER << std::endl;
#elif defined(__clang__)
    std::cout << "Compiler: Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__ << std::endl;
#else
    std::cout << "Compiler: Unknown" << std::endl;
#endif

    // 显示平台信息
#ifdef _WIN32
    std::cout << "Platform: Windows 32-bit" << std::endl;
#elif defined(_WIN64)
    std::cout << "Platform: Windows 64-bit" << std::endl;
#elif defined(__linux__)
    std::cout << "Platform: Linux" << std::endl;
#elif defined(__APPLE__)
    std::cout << "Platform: macOS" << std::endl;
#elif defined(__FreeBSD__)
    std::cout << "Platform: FreeBSD" << std::endl;
#else
    std::cout << "Platform: Unknown" << std::endl;
#endif
}