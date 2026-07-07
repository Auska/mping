# mping - Multi-host Ping Tool 开发规范

## 快速参考

```bash
# 构建（SQLite only, Release）
xmake

# 构建（含 PostgreSQL）
xmake f --use_postgresql=y && xmake

# 构建（含测试）
xmake f --build_tests=y && xmake

# 构建（含测试 + PostgreSQL）
xmake f --build_tests=y --use_postgresql=y && xmake

# 交叉编译（x86_64-musl, /opt/x-tools）
xmake f --use_cross=y && xmake

# 运行全部测试
xmake run mping_tests

# 运行单个测试（带详细输出）
xmake run mping_tests "[test_case_name]" -s

# 格式化代码
clang-format -i -style=file src/*.cpp src/*.h tests/*.cpp

# 检查格式化（逐文件对比）
for f in src/*.cpp src/*.h tests/*.cpp; do clang-format -style=file "$f" | diff - "$f"; done

# 安装到系统（需 root）
sudo xmake install
clang-format -i -style=file src/*.cpp src/*.h tests/*.cpp

# 检查格式化（逐文件对比）
for f in src/*.cpp src/*.h tests/*.cpp; do clang-format -style=file "$f" | diff - "$f"; done

```

## 项目概述
mping 是一个命令行工具，用于同时检查多个主机的连接性。它从文件中读取 IP 地址和主机名列表，并并发执行 ping 操作。该工具还提供数据库日志记录和查询功能以分析 ping 结果。

**当前版本**: 1.1.0  
**项目主页**: https://github.com/Auska/mping

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
- **`database_manager.cpp`/`database_manager.h`**：SQLite 数据库操作
- **`database_manager_pg.cpp`/`database_manager_pg.h`**：PostgreSQL 数据库操作
- **`config_manager.cpp`/`config_manager.h`**：配置管理（支持 XDG 规范的配置文件）
- **`config_file.cpp`/`config_file.h`**：INI 格式配置文件解析器（遵循 XDG 规范）
- **`database_factory.cpp`/`database_factory.h`**：数据库工厂模式实现
- **`database_interface.h`**：数据库抽象接口
- **`database_base.h`**：数据库基类，提供公共逻辑和 IP 验证
- **`version_info.cpp`/`version_info.h`**：版本信息管理

### 程序流程（main.cpp）

```
解析命令行参数（ConfigManager）
  → 加载配置文件（ConfigFile，XDG 规范搜索路径）
  → 命令行选项覆盖配置文件设置
  ┌── [queryIP 非空] → 创建 QueryIPCommand → 查询统计 → 结束
  ├── [cleanupDays >= 0] → 创建 CleanupCommand → 清理数据 → 结束
  ├── [queryAlerts 启用] → 创建 QueryAlertsCommand → 查询告警 → 结束
  ├── [queryRecoveryRecords 启用] → 创建 QueryRecoveryCommand → 查询恢复 → 结束
  └── [默认] → 创建 PingCommand
       → 确定数据库类型（DatabaseFactory，根据连接字符串自动检测）
       → 初始化数据库（创建表结构）
       → 读取主机列表（文件或数据库 hosts 表）
       → 执行并发 ping（PingManager，线程池，最大并发数 50）
       → 批量写入结果到数据库
       → 处理告警（带状态检测，只写入状态变化）
       → 清理过期 ping 记录
       → 输出结果
```

### 命令模式优势
- **降低耦合**：每种操作模式封装为独立命令类
- **简化测试**：命令类可以独立测试
- **降低复杂度**：`main()` 圈复杂度从 30 降至 5
- **易于扩展**：添加新命令只需继承 `Command` 并实现 `execute()`

### 数据库抽象层

```
DatabaseInterface（纯虚接口，9 个虚方法）
       ↑
DatabaseBase（公共逻辑: IP 验证, dbMutex, 禁止拷贝/移动）
       ↑
  ┌────┴────────────────────┐
  │                         │
DatabaseManager        DatabaseManagerPG
（SQLite, sqlite3*）    （PostgreSQL, PGconn*）
表名: ip_x_x_x_x       表名: ping_x_x_x_x
```

### 条件编译

代码中使用 `#ifdef USE_POSTGRESQL` / `#ifdef USE_SQLITE` 宏进行条件编译。两个宏**独立检测**，可同时定义（测试目标）以支持两种后端。添加新数据库支持时需要修改：
- `database_factory.cpp`：添加新的 `DatabaseType` 枚举值和创建逻辑
- `xmake.lua`：添加编译选项、依赖查找和源文件

## 编程规范
1. **使用 C++23 标准实现**：利用现代 C++ 特性和性能优化
2. **commit 必须使用英文**：提交信息应清晰描述变更内容
3. **使用面向对象方式实现**：采用封装、继承、多态等 OOP 原则
4. **优先使用项目中存在的函数**：避免重复造轮子，重用现有代码
5. **遵循工厂模式**：使用 DatabaseFactory 创建数据库实例
6. **使用现代 C++ 特性**：如 `std::println`、智能指针、移动语义等
7. **线程安全**：使用互斥锁和条件变量确保线程安全
8. **使用 Catch2 v3 进行单元测试**：测试框架使用 Catch2 v3.5.0
9. **代码格式化**：使用 clang-format 保持代码风格一致（Google 风格基础）
10. **PostgreSQL 时间列必须使用 TIMESTAMPTZ**：所有 PostgreSQL 表的时间列默认使用 `TIMESTAMPTZ` 类型，禁止使用 `TIMESTAMP`（不带时区）。`TIMESTAMPTZ` 原生以 UTC 存储，无需手动 `AT TIME ZONE 'UTC'` 转换。新增迁移逻辑时，需在 `migrateSchema()` 中处理列类型转换

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

### xmake.lua 规范
遵循 xmake-style 规范：
- **声明式配置**：`target()` / `option()` 内部仅使用 `set_` / `add_` 声明式调用，复杂逻辑放到 `on_load` 或独立脚本
- **`add_requires` 集中到文件顶部**：所有依赖包在 root scope 统一声明，不分散到 target 内部
- **`_end()` 按需使用**：`target_end()` / `option_end()` 不需要显式调用（除非条件 target 导致嵌套），xmake 通过下一个顶层调用自动闭合
- **缩进 4 空格**：无 Tab
- **依赖通过 xrepo 管理**：使用 `add_packages("sqlite3")` 而非 `add_syslinks("sqlite3")`，路径和标志由 xrepo 自动处理
- **平台条件使用 `is_plat`**：避免手动检测 `os.host()`

## 项目特性
- 并发 ping 多个主机以获得更快的结果（默认最大并发数 50）
- 从文件读取 IP 地址和主机名
- 显示所有主机的状态和响应时间
- 支持 SQLite 或 PostgreSQL 数据库日志记录
- 为特定 IP 地址查询统计信息
- 可配置的 ping 操作超时时间（默认 3 秒）
- 可配置的 ping 包数量（默认 3 个包）
- 主机状态监控和告警功能
- 恢复记录跟踪
- 数据自动清理功能
- 线程池优化的并发实现（默认最大并发数 50）
- 时区处理和时间戳记录功能（所有写入数据库的时间都使用 UTC 时间）
- 支持通过 `xmake install` 安装到系统
- **配置文件支持**：遵循 XDG 规范的配置文件管理
  - 支持 INI 格式配置文件
  - 自动从 XDG 配置目录加载配置
  - 支持命令行选项覆盖配置文件设置
  - 支持保存当前配置到文件

## 构建系统
- **Xmake**：使用 Xmake 作为构建系统
- **C++23 标准**（必需）
- **依赖管理**：通过 xrepo（Xmake 内置包管理器）自动下载，无需手动安装系统开发库
- **支持构建类型**：Release（默认）和 Debug
- **编译选项**：
  - `--use_postgresql=y`：启用 PostgreSQL 支持
  - `--build_tests=y`：编译测试程序
  - `-m debug`：调试版本构建
- **安装支持**：支持通过 `xmake install` 安装到系统

### 构建命令
```bash
# 仅启用 SQLite（默认）
xmake

# 启用 PostgreSQL 支持
xmake f --use_postgresql=y && xmake

# 调试版本构建
xmake f -m debug && xmake

# 安装到系统（需要 root 权限）
sudo xmake install
```

### 构建优化
- **Release 模式**：`-O3 -DNDEBUG` 优化标志
- **Debug 模式**：`-g -O0` 调试标志
- **并行编译**：Xmake 自动使用多核编译

## 依赖项
- **C++23 兼容编译器**（GCC 13+、Clang 16+ 或 MSVC 2022+）
- **Xmake**（内置 xrepo 包管理器，自动处理所有 C/C++ 依赖）
- **线程库**（pthread，系统内置）

所有第三方依赖（SQLite3、libpq、Catch2）均由 xrepo 在构建时自动下载和编译，**无需手动安装系统开发库**。xmake.lua 中使用 `add_requires` 声明依赖，`add_packages` 在目标中引用：

```lua
add_requires("sqlite3")
add_requires("libpq")
add_requires("catch2")

target("mping")
    add_packages("sqlite3")  -- 或 add_packages("libpq")
```

## 命令行选项
- `-h`, `--help`: 显示帮助信息
- `-v`, `--version`: 显示版本信息
- `-d`, `--database`: 启用数据库日志记录并指定数据库路径/连接字符串
- `-f`, `--file`: 指定包含主机的输入文件（默认：ip.txt）
- `-q`, `--query`: 查询特定 IP 地址的统计信息（需要 -d）
- `-a`, `--alerts [n]`: 查询活动告警（需要 -d，n: 天数，默认：全部）
- `-r`, `--recovery [n]`: 查询恢复记录（需要 -d，n: 天数，默认：全部）
- `-C`, `--cleanup [n]`: 清理 n 天前的数据（需要 -d，默认：30 天）
- `-s`, `--silent`: 静默模式，抑制输出
- `-c`, `--config <path>`: 从指定路径加载配置文件
- `-N`, `--no-config`: 不加载配置文件
- `-S`, `--save-config [path]`: 保存当前配置到文件（默认：XDG 配置目录）

> **注意**：PostgreSQL 支持通过连接字符串自动检测。当数据库路径包含 `host=` 时，将自动识别为 PostgreSQL 连接字符串。

## 文件格式
输入文件应包含以下格式的行：
```
# ip            hostname
10.224.1.11     test1
10.224.1.12     test2
```
以 `#` 开头的行被视为注释并忽略。

## 配置文件
mping 支持遵循 XDG 规范的配置文件，使用 INI 格式。

### 配置文件搜索路径（按优先级）
1. `$XDG_CONFIG_HOME/mping/config`
2. `$XDG_CONFIG_DIRS/mping/config`
3. `~/.config/mping/config`
4. `~/.mpingrc`
5. `./mping.conf`
6. `./.mpingrc`

### 配置文件格式
```ini
[general]
# 启用数据库日志记录
database = true

# 数据库路径
database_path = "/path/to/database.db"

# 静默模式
silent = false

# 清理 n 天前的数据
cleanup_days = 30
```

### 配置文件优先级
命令行选项的优先级高于配置文件。配置文件中的设置会被命令行选项覆盖。

### 保存配置
使用 `-S` 或 `--save-config` 选项保存当前配置：
```bash
# 保存到默认路径（$XDG_CONFIG_HOME/mping/config）
mping -S

# 保存到指定路径
mping -S /path/to/config.conf
```

## 数据库架构
- **`hosts` 表**：存储 IP 地址和主机名及创建和最后访问时间戳
- **`ping_results` 表**：统一存储所有 ping 结果（IP、主机名、延迟、成功状态和时间戳）
- **`alerts` 表**：跟踪主机状态告警
- **`recovery_records` 表**：记录主机从故障中恢复的信息

### 数据清理
`-C` / `--cleanup` 命令清理以下表中的旧数据：
- **`ping_results`**：删除超过指定天数的记录
- **`alerts`**：删除超过指定天数的告警记录
- **`recovery_records`**：删除超过指定天数的恢复记录

> **注意**：`hosts` 表不会被清理，以保留主机列表信息。

## 测试
项目使用 Catch2 v3.5.0 测试框架进行单元测试。

### 测试文件
- **`tests/test_main.cpp`**：测试入口，定义 `CATCH_CONFIG_MAIN`
- **`tests/test_commands.cpp`**：命令模式测试（所有 5 个命令）
- **`tests/test_database_manager.cpp`**：数据库管理器功能测试（含告警生命周期、并发访问）
- **`tests/test_ping_manager.cpp`**：Ping 管理器功能测试
- **`tests/test_utils.cpp`**：工具函数测试
- **`tests/test_config_manager.cpp`**：配置管理器测试（13 个测试用例）
- **`tests/test_version_info.cpp`**：版本信息测试
- **`tests/test_config_file.cpp`**：配置文件解析器测试（含原子写入验证）

当前共 **55 个测试用例**，**511 个断言**。

> **注意**：启用 PostgreSQL 时，测试程序会同时链接 SQLite 和 PostgreSQL 数据库管理器，以便进行跨数据库测试。`database_factory.cpp` 使用独立检测的 `USE_SQLITE` / `USE_POSTGRESQL` 宏，两个后端可同时编译。

### 运行测试
```bash
# 编译测试程序
xmake f --build_tests=y && xmake

# 运行所有测试
xmake run mping_tests

# 运行特定测试（使用 Catch2 过滤器）
xmake run mping_tests "[test_case_name]"

# 显示详细测试输出
xmake run mping_tests -s

# 列出所有测试用例
xmake run mping_tests --list-tests
```

## 开发实践
- **代码格式化**：使用 clang-format 保持代码风格一致（Google 风格基础）
- **并发设计**：使用线程池和异步操作以提高性能
- **错误处理**：全面的异常处理和错误报告机制
- **资源管理**：使用智能指针进行自动内存管理
- **配置管理**：通过 ConfigManager 处理命令行参数和配置
- **数据库抽象**：通过 DatabaseInterface 抽象不同数据库后端
- **数据库基类**：通过 DatabaseBase 提供公共逻辑（如 IP 验证）
- **现代 C++ 特性**：使用 C++23 标准的新特性，如 `std::println` 用于格式化输出
- **时间处理**：所有时间戳都使用 UTC 时间以确保跨时区的一致性
- **PostgreSQL 时间列**：所有 PostgreSQL 表的时间列默认使用 `TIMESTAMPTZ` 类型，禁止使用 `TIMESTAMP`（不带时区），`TIMESTAMPTZ` 原生以 UTC 存储，无需手动 `AT TIME ZONE 'UTC'` 转换
- **工厂模式**：使用 DatabaseFactory 根据连接字符串自动检测数据库类型
- **线程安全**：使用互斥锁（`std::mutex`）和条件变量（`std::condition_variable`）确保线程安全
- **测试驱动开发**：使用 Catch2 框架编写和运行单元测试

## 数据库连接字符串格式

### SQLite
```
/path/to/database.db
```

### PostgreSQL
```
host=localhost user=myuser password=mypass dbname=mydb
```

> **自动检测**：程序会自动根据连接字符串判断数据库类型。如果路径包含 `host=` 关键字，则识别为 PostgreSQL 连接。

### PostgreSQL 可选参数
- `client_min_messages=warning`：抑制 NOTICE 消息
- `port=5432`：指定端口
- `sslmode=require`：启用 SSL

## 安装
```bash
# 编译项目
xmake

# 安装到系统（需要 root 权限）
sudo xmake install
```

安装后，可执行文件将位于：
- 默认安装路径：`/usr/local/bin/mping`
- 自定义安装路径：`$CMAKE_INSTALL_PREFIX/bin/mping`

## 版本信息
项目版本信息通过 xmake 编译定义传递：
- `PROJECT_NAME`：项目名称
- `PROJECT_VERSION`：主版本号
- `PROJECT_VERSION_MAJOR`：主版本号
- `PROJECT_VERSION_MINOR`：次版本号
- `PROJECT_VERSION_PATCH`：补丁版本号
- `PROJECT_DESCRIPTION`：项目描述
- `PROJECT_HOMEPAGE_URL`：项目主页
- `COMPILE_TIME`：编译时间戳

这些定义在编译时通过 `add_defines` 传递给源代码。
