#include "version_info.h"

#include <iostream>
#include <print>

void print_version_info() {
    std::println(std::cout, "{} version {}", PROJECT_NAME, PROJECT_VERSION);
    std::println(std::cout, "Description: {}", PROJECT_DESCRIPTION);
    std::println(std::cout, "Homepage: {}", PROJECT_HOMEPAGE_URL);
    std::println(std::cout, "Version: {}.{}.{}", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR,
                 PROJECT_VERSION_PATCH);
    std::println(std::cout, "Build Time: {}", COMPILE_TIME);

    // 显示编译器信息
#ifdef __GNUC__
    std::println(std::cout, "Compiler: GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__,
                 __GNUC_PATCHLEVEL__);
#elif defined(__clang__)
    std::println(std::cout, "Compiler: Clang {}.{}.{}", __clang_major__, __clang_minor__,
                 __clang_patchlevel__);
#else
    std::println(std::cout, "Compiler: Unknown");
#endif

    // 显示数据库后端信息
    std::println(std::cout, "Database: PostgreSQL");

    // 显示平台信息
#ifdef __linux__
    std::println(std::cout, "Platform: Linux");
#elif defined(__APPLE__)
    std::println(std::cout, "Platform: macOS");
#elif defined(__FreeBSD__)
    std::println(std::cout, "Platform: FreeBSD");
#else
    std::println(std::cout, "Platform: Unknown");
#endif
}