add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

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

add_requires("sqlite3", {configs = {shared = false}, system = false})
add_requires("libpq")
add_requires("catch2", {configs = {shared = false}, system = false})
add_requires("fmt")

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

    if has_config("use_postgresql") then
        add_files("src/database_manager_pg.cpp")
        add_defines("USE_POSTGRESQL=1")
        add_packages("libpq")
    else
        add_files("src/database_manager.cpp")
        add_defines("USE_SQLITE=1")
        add_packages("sqlite3")
    end

    add_packages("fmt")

    if is_plat("linux") then
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

        if has_config("use_postgresql") then
            add_files("src/database_manager_pg.cpp", "src/database_manager.cpp")
            add_defines("USE_POSTGRESQL=1", "USE_SQLITE=1")
            add_packages("libpq", "sqlite3", "catch2", "fmt")
        else
            add_files("src/database_manager.cpp")
            add_defines("USE_SQLITE=1")
            add_packages("sqlite3")
            add_packages("catch2")
            add_packages("fmt")
        end

        if is_plat("linux") then
            add_syslinks("pthread")
        end
end
