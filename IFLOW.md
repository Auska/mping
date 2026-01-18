# mping - Multi-host Ping Tool 开发规范

## 项目概述
mping 是一个命令行工具，用于同时检查多个主机的连接性。它从文件中读取 IP 地址和主机名列表，并并发执行 ping 操作。该工具还提供数据库日志记录和查询功能以分析 ping 结果。

**当前版本**: 1.1.0  
**项目主页**: https://github.com/Auska/mping

## 架构设计
- **模块化设计**：分离关注点，各模块职责明确
- **`main.cpp`**：命令行界面和参数解析
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

### 格式化检查（在 CI/CD 中）
```bash
# 检查是否需要格式化（不修改文件）
clang-format -style=file src/*.cpp src/*.h tests/*.cpp | diff - src/*.cpp
```

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
- 支持通过 `make install` 安装到系统
- **配置文件支持**：遵循 XDG 规范的配置文件管理
  - 支持 INI 格式配置文件
  - 自动从 XDG 配置目录加载配置
  - 支持命令行选项覆盖配置文件设置
  - 支持保存当前配置到文件

## 构建系统
- **CMake 3.10+**：使用 CMake 作为构建系统
- **C++23 标准**：`CMAKE_CXX_STANDARD 23`（必需）
- **支持构建类型**：Release（默认）和 Debug
- **编译选项**：
  - `-DUSE_POSTGRESQL=ON`：启用 PostgreSQL 支持
  - `-DBUILD_TESTS=ON`：编译测试程序
- **安装支持**：支持通过 `make install` 安装到系统

### 构建命令
```bash
# 创建构建目录
mkdir build && cd build

# 仅启用 SQLite（默认）
cmake ..
make -j$(nproc)

# 启用 PostgreSQL 支持
cmake -DUSE_POSTGRESQL=ON ..
make -j$(nproc)

# 调试版本构建
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# 使用构建脚本（支持 Debug/Release）
./build.sh [Debug|Release]

# 安装到系统（需要 root 权限）
sudo make install
```

### 构建优化
- **Release 模式**：`-O3 -DNDEBUG` 优化标志
- **Debug 模式**：`-g -O0` 调试标志
- **并行编译**：使用 `make -j$(nproc)` 加速编译

## 依赖项
- **CMake 3.10 或更高版本**
- **C++23 兼容编译器**（GCC 13+、Clang 16+ 或 MSVC 2022+）
- **SQLite3 开发库**（必需）
- **PostgreSQL 开发库**（可选，用于 PostgreSQL 支持）
- **线程库**（pthread，用于并发处理）
- **pkg-config**（可选，用于 PostgreSQL 支持）

### 系统依赖安装
```bash
# Debian/Ubuntu
sudo apt-get install cmake build-essential libsqlite3-dev libpq-dev pkg-config

# Fedora/RHEL
sudo dnf install cmake gcc-c++ sqlite-devel postgresql-devel pkg-config

# macOS
brew install cmake sqlite postgresql pkg-config
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
- `-n`, `--count <n>`: 每个主机发送的 ping 包数量（默认：3）
- `-t`, `--timeout <n>`: 每个 ping 的超时时间（秒，默认：3）
- `-c`, `--config <path>`: 从指定路径加载配置文件
- `-N`, `--no-config`: 不加载配置文件
- `-S`, `--save-config [path]`: 保存当前配置到文件（默认：XDG 配置目录）
- `-P`, `--postgresql`: 使用 PostgreSQL 数据库（需要 -d 与连接字符串）

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

# 每个主机发送的 ping 包数量
ping_count = 3

# 每个 ping 的超时时间（秒）
timeout = 3

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
- **IP 特定表**：每个 IP 有自己的表（SQLite 中为 `ip_x_x_x_x`，PostgreSQL 中为 `ping_x_x_x_x`）存储 ping 结果（延迟、成功状态和时间戳）
- **告警表**：跟踪主机状态告警
- **恢复记录表**：记录主机从故障中恢复的信息

### SQLite vs PostgreSQL 表命名
- **SQLite**: `ip_10_224_1_11`
- **PostgreSQL**: `ping_10_224_1_11`

## 测试
项目使用 Catch2 v3.5.0 测试框架进行单元测试。

### 测试文件
- **`tests/test_main.cpp`**：测试入口，定义 `CATCH_CONFIG_MAIN`
- **`tests/test_database_manager.cpp`**：数据库管理器功能测试
- **`tests/test_ping_manager.cpp`**：Ping 管理器功能测试
- **`tests/test_utils.cpp`**：工具函数测试
- **`tests/test_config_manager.cpp`**：配置管理器测试
- **`tests/test_version_info.cpp`**：版本信息测试
- **`tests/test_config_file.cpp`**：配置文件解析器测试

> **注意**：启用 PostgreSQL 时，测试程序会同时链接 SQLite 和 PostgreSQL 数据库管理器，以便进行跨数据库测试。

### 运行测试
```bash
# 编译测试程序
cd build
cmake -DBUILD_TESTS=ON ..
make -j$(nproc) mping_tests

# 运行所有测试
./mping_tests

# 运行特定测试（使用 Catch2 过滤器）
./mping_tests "[test_case_name]"

# 显示详细测试输出
./mping_tests -s

# 列出所有测试用例
./mping_tests --list-tests
```

### 测试自动发现
项目使用 CTest 和 Catch2 的自动测试发现功能。编译时，Catch2 会自动发现所有测试用例并注册到 CTest。

```bash
# 使用 CTest 运行测试
cd build
ctest --output-on-failure

# 运行特定测试
ctest -R test_case_name

# 显示详细输出
ctest --verbose
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

### PostgreSQL 可选参数
- `client_min_messages=warning`：抑制 NOTICE 消息
- `port=5432`：指定端口
- `sslmode=require`：启用 SSL

## 安装
```bash
# 编译项目
mkdir build && cd build
cmake ..
make -j$(nproc)

# 安装到系统（默认：/usr/local）
sudo make install

# 自定义安装路径
cmake -DCMAKE_INSTALL_PREFIX=/opt/mping ..
make -j$(nproc)
sudo make install
```

安装后，可执行文件将位于：
- 默认安装路径：`/usr/local/bin/mping`
- 自定义安装路径：`$CMAKE_INSTALL_PREFIX/bin/mping`

## 版本信息
项目版本信息通过 CMake 编译定义传递：
- `PROJECT_NAME`：项目名称
- `PROJECT_VERSION`：主版本号
- `PROJECT_VERSION_MAJOR`：主版本号
- `PROJECT_VERSION_MINOR`：次版本号
- `PROJECT_VERSION_PATCH`：补丁版本号
- `PROJECT_DESCRIPTION`：项目描述
- `PROJECT_HOMEPAGE_URL`：项目主页
- `COMPILE_TIME`：编译时间戳

这些定义在编译时通过 `target_compile_definitions` 传递给源代码。