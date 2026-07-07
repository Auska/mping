add_rules("mode.debug", "mode.release")

set_languages("c++23")
set_warnings("all", "extra", "pedantic")

option("use_postgresql")
    set_default(false)
    set_showmenu(true)
    set_description("Use PostgreSQL database (default is SQLite)")
option_end()

option("build_tests")
    set_default(false)
    set_showmenu(true)
    set_description("Build test programs")
option_end()

option("use_cross")
    set_default(false)
    set_showmenu(true)
    set_description("Cross-compile with /opt/x-tools x86_64-musl toolchain")
option_end()

toolchain("x86_64-musl")
    set_kind("standalone")
    set_sdkdir("/opt/x-tools/x86_64-unknown-linux-musl")
    set_toolset("cc", "x86_64-unknown-linux-musl-gcc")
    set_toolset("cxx", "x86_64-unknown-linux-musl-g++")
    set_toolset("ld", "x86_64-unknown-linux-musl-g++")
    set_toolset("ar", "x86_64-unknown-linux-musl-ar")
    set_toolset("sh", "x86_64-unknown-linux-musl-g++")
    set_toolset("strip", "x86_64-unknown-linux-musl-strip")
    set_toolset("as", "x86_64-unknown-linux-musl-as")
    on_load(function (toolchain)
        local sdk = toolchain:sdkdir()
        if sdk then
            toolchain:add("includedirs", path.join(sdk, "x86_64-unknown-linux-musl", "sysroot", "usr", "include"))
            toolchain:add("linkdirs", path.join(sdk, "x86_64-unknown-linux-musl", "sysroot", "usr", "lib"))
        end
        toolchain:add("ldflags", "-static")
    end)
toolchain_end()

add_requires("sqlite3", {configs = {shared = false}, system = false})
if not has_config("use_cross") then
    add_requires("libpq", {configs = {shared = false}, system = false})
    add_requires("catch2", {configs = {shared = false}, system = false})
end

local compile_time = os.date("%Y-%m-%d %H:%M:%S")
local common_sources = {
    "src/commands.cpp",
    "src/database_factory.cpp",
    "src/ping_manager.cpp",
    "src/config_manager.cpp",
    "src/config_file.cpp",
    "src/version_info.cpp",
    "src/utils.cpp"
}
local common_defines = {
    "PROJECT_NAME=\"mping\"",
    "PROJECT_VERSION=\"1.1.0\"",
    "PROJECT_VERSION_MAJOR=1",
    "PROJECT_VERSION_MINOR=1",
    "PROJECT_VERSION_PATCH=0",
    "PROJECT_DESCRIPTION=\"Multi-host Ping Tool\"",
    "PROJECT_HOMEPAGE_URL=\"https://github.com/Auska/mping\"",
    "COMPILE_TIME=\"" .. compile_time .. "\""
}

target("mping")
    set_kind("binary")
    add_includedirs("src", "include")
    add_files("src/main.cpp")
    add_files(common_sources)
    add_defines(common_defines)

    if has_config("use_cross") then
        set_plat("cross")
        set_arch("x86_64")
        set_toolchains("x86_64-musl")
    end

    if has_config("use_postgresql") then
        add_files("src/database_manager_pg.cpp")
        add_defines("USE_POSTGRESQL=1")
        if not has_config("use_cross") then
            add_packages("libpq")
        end
    else
        add_files("src/database_manager.cpp")
        add_defines("USE_SQLITE=1")
        add_packages("sqlite3")
    end

    if is_plat("linux") and not has_config("use_cross") then
        add_syslinks("pthread")
    end

target_end()

if has_config("build_tests") then
    target("mping_tests")
        set_kind("binary")
        add_includedirs("src", "include")
        add_files(common_sources)
        add_defines(common_defines)
        add_files(
            "tests/test_main.cpp",
            "tests/test_commands.cpp",
            "tests/test_database_manager.cpp",
            "tests/test_ping_manager.cpp",
            "tests/test_utils.cpp",
            "tests/test_config_manager.cpp",
            "tests/test_version_info.cpp",
            "tests/test_config_file.cpp"
        )

        if has_config("use_cross") then
            set_plat("cross")
            set_arch("x86_64")
            set_toolchains("x86_64-musl")
        end

        if has_config("use_postgresql") then
            add_files("src/database_manager_pg.cpp", "src/database_manager.cpp")
            add_defines("USE_POSTGRESQL=1", "USE_SQLITE=1")
            if not has_config("use_cross") then
                add_packages("libpq", "sqlite3", "catch2")
            end
        else
            add_files("src/database_manager.cpp")
            add_defines("USE_SQLITE=1")
            add_packages("sqlite3")
            if not has_config("use_cross") then
                add_packages("catch2")
            end
        end

        if is_plat("linux") and not has_config("use_cross") then
            add_syslinks("pthread")
        end
end
