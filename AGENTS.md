# mping - Multi-host Ping Tool 开发规范

## 快速参考

```bash
# 配置并构建（PostgreSQL 是唯一数据库后端, Release；零警告构建 -Werror）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # 或 cmake --preset release
cmake --build build -j   # 或 cmake --build --preset release

# 测试构建与 ASan/UBSan 检查（CMake Presets）
cmake --preset debug     # Debug + 单测（build-debug/）
cmake --preset asan      # ASan/UBSan + 单测（build-asan/）
cmake --build --preset asan
ctest --test-dir build-asan

# 构建（含测试）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMPING_BUILD_TESTS=ON
cmake --build build -j

# 运行全部测试（CTest）
ctest --test-dir build

# 运行单个测试（带详细输出）
./build/mping_tests "[test_case_name]" -s

# 格式化代码
clang-format -i -style=file src/*.cpp src/*.h tests/*.cpp

# 检查格式化（逐文件对比）
for f in src/*.cpp src/*.h tests/*.cpp; do clang-format -style=file "$f" | diff - "$f"; done

# 安装到系统（需 root）
sudo cmake --install build

```

## 项目概述

mping 是一个命令行工具，用于同时检查多个主机的连接性。它从文件中读取 IP 地址和主机名列表，并并发执行 ping 操作。该工具还提供数据库日志记录和查询功能以分析 ping 结果。

**当前版本**: 1.1.0  
**项目主页**: <https://github.com/Auska/mping>

## 架构设计

- **模块化设计**：分离关注点，各模块职责明确
- **命令模式**：`commands.h`/`commands.cpp` 定义了 Command 抽象基类和 5 个子命令
  - `QueryIPCommand`：查询 IP 统计
  - `CleanupCommand`：清理旧数据
  - `QueryAlertsCommand`：查询告警
  - `QueryRecoveryCommand`：查询恢复记录
  - `PingCommand`：执行 Ping 并存储结果
- **`main.cpp`**：精简至 ~50 行，仅负责参数解析和命令调度
- **`utils.cpp`/`utils.h`**：文件操作等实用函数
- **`ping_manager.cpp`/`ping_manager.h`**：核心 ping 功能实现（线程池优化）
- **`database_manager_pg.cpp`/`database_manager_pg.h`**：PostgreSQL 数据库操作
- **`config_manager.cpp`/`config_manager.h`**：配置管理
- **`config_file.cpp`/`config_file.h`**：INI 格式配置文件解析器
- **`ip_validator.h`**：IP 地址校验工具
- **`version_info.cpp`/`version_info.h`**：版本信息管理

### 程序流程（main.cpp）

```
解析命令行参数（ConfigManager）
  → 加载配置文件（ConfigFile，默认 $HOME/.config/mping/config.ini）
  → 命令行选项覆盖配置文件设置
  ┌── [queryIP 非空] → 创建 QueryIPCommand → 查询统计 → 结束
  ├── [cleanupDays >= 0 且非持续模式] → 创建 CleanupCommand → 清理数据 → 结束（持续模式下每轮内执行清理，不退出）
  ├── [queryAlerts 启用] → 创建 QueryAlertsCommand → 查询告警 → 结束
  ├── [queryRecoveryRecords 启用] → 创建 QueryRecoveryCommand → 查询恢复 → 结束
  └── [默认] → 创建 PingCommand
       → 创建 PostgreSQL 数据库实例（DatabaseManagerPG）
       → 初始化数据库（创建表结构）
       → 读取主机列表（文件或数据库 hosts 表）
       → 执行并发 ping（PingManager，线程池，最大并发数 50）
       → 滑动窗口确认（未告警主机连续 3 轮失败才判定离线，对抗瞬时波动；已告警主机单轮快检）
       → 批量写入结果到数据库
       → 处理告警（带状态检测，只写入状态变化）
       → 清理过期 ping 记录
       → 输出结果
       → [check_interval > 0] 等待间隔秒数后回到"读取主机列表"循环（持续检查模式）
```

### 命令模式优势

- **降低耦合**：每种操作模式封装为独立命令类
- **简化测试**：命令类可以独立测试
- **降低复杂度**：`main()` 圈复杂度从 30 降至 5
- **易于扩展**：添加新命令只需继承 `Command` 并实现 `execute()`

### 数据库层

```
DatabaseManagerPG（PostgreSQL, PGconn*）
表名: hosts / ping_results（按日分区）/ alerts / recovery_records
IP 校验: IPValidator（ip_validator.h）
```

### 数据库后端

PostgreSQL 是唯一数据库后端，始终编译（`find_package(PostgreSQL REQUIRED)`），数据库操作全部通过 `DatabaseManagerPG` 完成。如需新增其他后端，需重新引入抽象接口并调整 `commands.cpp` 的调用：
- `CMakeLists.txt`：添加依赖查找和源文件

## 编程规范

1. **使用 C++23 标准实现**：利用现代 C++ 特性和性能优化
2. **commit 必须使用英文**：提交信息应清晰描述变更内容
3. **使用面向对象方式实现**：采用封装、继承、多态等 OOP 原则
4. **优先使用项目中存在的函数**：避免重复造轮子，重用现有代码
5. **数据库访问统一走 DatabaseManagerPG**：数据库操作集中在该类封装方法中，禁止在调用方手工拼装 SQL
6. **使用现代 C++ 特性**：如 `std::println`、智能指针、移动语义等
7. **线程安全**：使用互斥锁和条件变量确保线程安全
8. **使用 Catch2 v3 进行单元测试**：测试框架使用 Catch2 v3
9. **代码格式化**：使用 clang-format 保持代码风格一致（Google 风格基础）
10. **PostgreSQL 时间列必须使用 TIMESTAMPTZ**：所有 PostgreSQL 表的时间列默认使用 `TIMESTAMPTZ` 类型，禁止使用 `TIMESTAMP`（不带时区）。`TIMESTAMPTZ` 原生以 UTC 存储，无需手动 `AT TIME ZONE 'UTC'` 转换。连接建立后必须在 `initialize()` 中执行 `SET TIME ZONE 'UTC'`（会话级），确保写入的 UTC 文本时间戳按 UTC 解释，不受服务器默认时区影响。新增迁移逻辑时，需在 `migrateSchema()` 中处理列类型转换

### 代码格式化规范

项目使用 `.clang-format` 配置文件定义代码风格标准：

```bash
# 格式化所有源文件
clang-format -i -style=file src/*.cpp src/*.h tests/*.cpp
```

**主要格式化规则**：

- 缩进：4 空格，不使用 Tab
- 括号风格：K&R 风格（左大括号不换行）
- 行宽限制：100 字符
- 指针对齐：左对齐 (`int* p;`)
- `#include` 自动排序和分组
- 连续赋值自动对齐
- 短函数可放在单行（inline only）

### 格式化检查

```bash
# 检查是否需要格式化（逐文件对比，不修改文件）
for f in src/*.cpp src/*.h tests/*.cpp; do clang-format -style=file "$f" | diff - "$f"; done
```

### CMakeLists.txt 规范

遵循 CMake 风格规范：

- **声明式配置**：`add_executable()` / `target_*()` 内部仅使用声明式调用，复杂逻辑放到函数或 `if()` 分支
- **`find_package()` 集中到文件顶部**：依赖查找统一声明，不分散到分支内部
- **target 属性使用 `target_*()` 命令**：使用 `target_include_directories` / `target_compile_definitions` / `target_link_libraries`，避免全局 `include_directories()` / `add_definitions()`
- **缩进 4 空格**：无 Tab
- **选项使用 `option()`**：`MPING_BUILD_TESTS` 通过 `-D` 传递
- **仅支持 POSIX 平台**：Windows 不支持，CMake 配置阶段用 `if(WIN32)` 直接拒绝；其余平台条件使用 `if(UNIX)`，避免手动检测编译器

## 项目特性

- 并发 ping 多个主机以获得更快的结果（默认最大并发数 50）
- 从文件读取 IP 地址和主机名
- 显示所有主机的状态和响应时间
- 支持 PostgreSQL 数据库日志记录
- 为特定 IP 地址查询统计信息
- 可配置的 ping 操作超时时间（默认 3 秒）
- 可配置的 ping 包数量（默认 3 个包）
- 主机状态监控和告警功能
- 恢复记录跟踪
- 数据自动清理功能（ping_results 按日分区，过期日分区整体 DROP）
- 线程池优化的并发实现（默认最大并发数 50）
- 时区处理和时间戳记录功能（所有写入数据库的时间都使用 UTC 时间）
- 支持通过 `cmake --install` 安装到系统
- **配置文件支持**：INI 格式配置文件（默认路径 $HOME/.config/mping/config.ini）
  - 自动从默认路径加载配置
  - 支持命令行选项覆盖配置文件设置
  - 支持保存当前配置到文件
- 持续检查模式：配置文件 `check_interval`（秒）> 0 时按固定间隔循环检查，主机清单与告警状态每轮重载，`-S` 可落盘

## 构建系统

- **CMake**：使用 CMake（>= 3.16）作为构建系统
- **C++23 标准**（必需）
- **依赖管理**：通过 `find_package` 查找系统开发库（libpq、Catch2、Threads）
- **支持构建类型**：Release（默认）和 Debug
- **编译选项**（通过 `-D` 传递）：
  - `MPING_BUILD_TESTS=ON`：编译测试程序
  - `-DCMAKE_BUILD_TYPE=Debug`：调试版本构建
- **安装支持**：支持通过 `cmake --install` 安装到系统

### 构建命令

```bash
# 配置并构建（PostgreSQL 是唯一数据库后端）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 调试版本构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 安装到系统（需要 root 权限）
sudo cmake --install build
```

### 构建优化

- **Release 模式**：`-O3 -DNDEBUG` 优化标志
- **Debug 模式**：`-g -O0` 调试标志
- **并行编译**：`cmake --build -j` 自动使用多核编译

## 依赖项

- **C++23 兼容编译器**（GCC 13+ 或 Clang 16+；不支持 MSVC/Windows）
- **CMake 3.16+**
- **线程库**（pthread，系统内置）
- libpq（PostgreSQL）、Catch2 开发库（通过系统包管理器安装）

第三方依赖通过 `find_package` 查找系统安装的开发库，CMakeLists.txt 中使用 imported target 进行链接：

```cmake
find_package(PostgreSQL REQUIRED)
target_link_libraries(mping PRIVATE PostgreSQL::PostgreSQL)
```

## 命令行选项

> **仅支持短选项**：`-h`/`-v`/`-f`/`-q`/`-a`/`-r`/`-C`/`-s`/`-c`/`-S`，不支持长选项（如 `--help`）。`-a`/`-r`/`-C` 的天数**仅支持附加形式**（如 `-C60`）；`-S` 的路径支持附加与空格分隔两种形式；剩余位置参数按文件名处理。数据库**只通过配置文件** `database_path` 配置（无 CLI 数据库选项）

- `-h`: 显示帮助信息
- `-v`: 显示版本信息
- `-f <file>`: 指定包含主机的输入文件（默认：ip.txt）
- `-q <ip>`: 查询特定 IP 地址的统计信息（需要配置文件 `database_path`）
- `-a [n]`: 查询活动告警（需要配置文件 `database_path`，n: 天数，默认：全部）
- `-r [n]`: 查询恢复记录（需要配置文件 `database_path`，n: 天数，默认：全部）
- `-C [n]`: 清理 n 天前的数据（需要配置文件 `database_path`，默认：30 天）
- `-s`: 静默模式，抑制输出
- `-c <path>`: 从指定路径加载配置文件
- `-S [path]`: 保存当前配置到文件（默认：`$HOME/.config/mping/config.ini`）

> **注意**：数据库通过配置文件 `database_path`（libpq 连接字符串格式）配置，如 `host=localhost user=myuser dbname=mydb`；未配置时数据库功能（查询/告警/清理/落库/DB 主机列表）不可用，ping 仅使用文件模式。

## 文件格式

输入文件应包含以下格式的行：

```
# ip            hostname
10.224.1.11     test1
10.224.1.12     test2
```

以 `#` 开头的行被视为注释并忽略。

## 配置文件

mping 支持配置文件（INI 格式），默认路径为 `$HOME/.config/mping/config.ini`。

### 配置文件格式

```ini
[general]
# PostgreSQL 连接字符串
database_path = "host=localhost user=myuser dbname=mydb"

# 静默模式
silent = false

# 清理 n 天前的数据
cleanup_days = 30

# 持续检查模式：>0 = 每轮检查间隔秒数；0 或缺失 = 单次运行
check_interval = 60
```

### 配置文件优先级

命令行选项的优先级高于配置文件。配置文件中的设置会被命令行选项覆盖。

> **注意**：数据库仅通过配置文件 `database_path` 键启用（CLI 无数据库选项）；`database_path`/`silent`/`cleanup_days`/`check_interval` 之外的其他键会被忽略。

### 保存配置

使用 `-S` 选项保存当前配置：

```bash
# 保存到默认路径（$HOME/.config/mping/config.ini）
mping -S

# 保存到指定路径
mping -S /path/to/config.conf
```

> 保存为原子写入（tmp + rename）；按节重写全部条目，文件中已有但未设置的键（未知键）随加载保留，注释与原始顺序不保留。

## 数据库架构

- **`hosts` 表**：存储 IP 地址和主机名及创建和最后访问时间戳
- **`ping_results` 表**：统一存储所有 ping 结果（IP、主机名、延迟、成功状态和时间戳）
  - **按日分区**：RANGE 分区（UTC 日界），分区名 `ping_results_YYYYMMDD`；`initialize()`（Ping 写入路径）预建今天起未来 30 天分区；查询/清理命令使用轻量 `initializeForQuery()`（建表+迁移，不预建分区）。插入遇缺失分区（SQLSTATE 23514）时批量插入回退到逐行 SAVEPOINT 并补建分区后重试；旧版普通表在初始化时自动迁移（改名保留数据、回填、序列对齐，幂等）
- **`alerts` 表**：跟踪主机状态告警
- **`recovery_records` 表**：记录主机从故障中恢复的信息
- **`mping_meta` 表**：内部元数据（如自动清理节流标记）

### 数据清理

`-C` 命令清理以下表中的旧数据：

- **`ping_results`**：删除超过指定天数的记录（整日早于截止期的分区直接 DROP，瞬时释放磁盘；边界日内过期行用 DELETE 精确删除）
- **`alerts`**：删除超过指定天数的告警记录
- **`recovery_records`**：删除超过指定天数的恢复记录

> **注意**：`hosts` 表不会被清理，以保留主机列表信息。

Ping 运行自动触发的 `ping_results` 过期清理带 **24 小时节流**（时间戳存于 `mping_meta`），避免高频运行重复查询；`-C` 显式清理不受节流限制。

## 测试

项目使用 Catch2 v3.5.0 测试框架进行单元测试。

### 测试文件

- **`tests/test_main.cpp`**：测试入口，定义 `CATCH_CONFIG_MAIN`
- **`tests/test_commands.cpp`**：命令模式测试（所有 5 个命令）
- **`tests/test_database_manager.cpp`**：数据库管理器与 IP 校验测试（连接/初始化行为）
- **`tests/test_ping_manager.cpp`**：Ping 管理器功能测试
- **`tests/test_utils.cpp`**：工具函数测试
- **`tests/test_config_manager.cpp`**：配置管理器测试（13 个测试用例）
- **`tests/test_version_info.cpp`**：版本信息测试
- **`tests/test_config_file.cpp`**：配置文件解析器测试（含原子写入验证）

当前共 **45 个测试用例**。其中 7 个数据库命令测试依赖真实 PostgreSQL 服务：通过环境变量 `MPING_TEST_PG_CONNSTR` 指定连接串（默认 `host=localhost user=postgres dbname=postgres`），服务不可达时自动跳过（SKIP），不影响其余测试。数据库命令测试全部启用时共 **333 个断言**。

退出码约定：`-h`/`-v`/`-S` 正常完成返回 0；未知选项、缺少必需参数、非法的 `-a`/`-r`/`-C` 天数等参数错误返回 1；命令执行失败（如主机文件不存在、数据库初始化失败）返回 1。

### 运行测试

```bash
# 编译测试程序
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMPING_BUILD_TESTS=ON
cmake --build build -j

# 运行所有测试（CTest）
ctest --test-dir build

# 运行含数据库命令的完整测试（需可用 PostgreSQL；6 个用例不可达时自动跳过）
MPING_TEST_PG_CONNSTR='host=localhost user=postgres dbname=postgres' ctest --test-dir build

# 运行特定测试（使用 Catch2 过滤器）
./build/mping_tests "[test_case_name]"

# 显示详细测试输出
./build/mping_tests -s

# 列出所有测试用例
./build/mping_tests --list-tests
```

## 开发实践

- **代码格式化**：使用 clang-format 保持代码风格一致（Google 风格基础）
- **并发设计**：使用线程池和异步操作以提高性能
- **错误处理**：全面的异常处理和错误报告机制
- **资源管理**：使用智能指针进行自动内存管理
- **配置管理**：通过 ConfigManager 处理命令行参数和配置
- **数据库访问**：全部通过 DatabaseManagerPG 封装（连接、建表、事务、参数化查询）；IP 校验用 IPValidator
- **现代 C++ 特性**：使用 C++23 标准的新特性，如 `std::println` 用于格式化输出
- **时间处理**：所有时间戳都使用 UTC 时间以确保跨时区的一致性；本地时间戳生成使用 `gmtime_r`（线程安全）
- **PostgreSQL 时间列**：所有 PostgreSQL 表的时间列默认使用 `TIMESTAMPTZ` 类型，禁止使用 `TIMESTAMP`（不带时区），`TIMESTAMPTZ` 原生以 UTC 存储，无需手动 `AT TIME ZONE 'UTC'` 转换；会话须 `SET TIME ZONE 'UTC'`（见 `initialize()`）
- **线程安全**：使用互斥锁（`std::mutex`）和条件变量（`std::condition_variable`）确保线程安全
- **测试驱动开发**：使用 Catch2 框架编写和运行单元测试

## 数据库连接字符串格式

PostgreSQL（libpq 连接字符串，唯一支持的数据库）：

```
host=localhost user=myuser password=mypass dbname=mydb
```

### 可选参数

- `client_min_messages=warning`：抑制 NOTICE 消息
- `port=5432`：指定端口
- `sslmode=require`：启用 SSL

## 安装

```bash
# 编译项目
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 安装到系统（需要 root 权限）
sudo cmake --install build
```

安装后，可执行文件将位于：

- 默认安装路径：`/usr/local/bin/mping`
- 自定义安装路径：`$CMAKE_INSTALL_PREFIX/bin/mping`

## 版本信息

项目版本信息通过 CMake 编译定义传递（由 `project()` 元数据自动生成）：

- `PROJECT_NAME`：项目名称
- `PROJECT_VERSION`：主版本号
- `PROJECT_VERSION_MAJOR`：主版本号
- `PROJECT_VERSION_MINOR`：次版本号
- `PROJECT_VERSION_PATCH`：补丁版本号
- `PROJECT_DESCRIPTION`：项目描述
- `PROJECT_HOMEPAGE_URL`：项目主页
- `COMPILE_TIME`：编译时间戳

这些定义在编译时通过 `add_defines` 传递给源代码。
