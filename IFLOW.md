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
- **`config_manager.cpp`/`config_manager.h`**：配置管理
- **`database_factory.cpp`/`database_factory.h`**：数据库工厂模式实现
- **`database_interface.h`**：数据库抽象接口
- **`version_info.cpp`/`version_info.h`**：版本信息管理

## 编程规范
1. **使用 C++23 标准实现**：利用现代 C++ 特性和性能优化
2. **commit 必须使用英文**：提交信息应清晰描述变更内容
3. **使用面向对象方式实现**：采用封装、继承、多态等 OOP 原则
4. **优先使用项目中存在的函数**：避免重复造轮子，重用现有代码
5. **遵循工厂模式**：使用 DatabaseFactory 创建数据库实例
6. **使用现代 C++ 特性**：如 `std::println`、智能指针、移动语义等
7. **线程安全**：使用互斥锁和条件变量确保线程安全

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
- 支持 pkg-config 安装

## 构建系统
- **CMake 3.10+**：使用 CMake 作为构建系统
- **C++23 标准**：`CMAKE_CXX_STANDARD 23`（必需）
- **支持构建类型**：Release（默认）和 Debug
- **编译选项**：
  - `-DUSE_POSTGRESQL=ON`：启用 PostgreSQL 支持
  - `-DBUILD_TESTS=ON`：编译测试程序
- **安装支持**：支持通过 `make install` 和 pkg-config 安装

### 构建命令
```bash
# 创建构建目录
mkdir build && cd build

# 仅启用 SQLite（默认）
cmake ..
make

# 启用 PostgreSQL 支持
cmake -DUSE_POSTGRESQL=ON ..
make

# 调试版本构建
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

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
- `-P`, `--postgresql`: 使用 PostgreSQL 数据库（需要 -d 与连接字符串）

## 文件格式
输入文件应包含以下格式的行：
```
# ip            hostname
10.224.1.11     test1
10.224.1.12     test2
```
以 `#` 开头的行被视为注释并忽略。

## 数据库架构
- **`hosts` 表**：存储 IP 地址和主机名及创建和最后访问时间戳
- **IP 特定表**：每个 IP 有自己的表（SQLite 中为 `ip_x_x_x_x`，PostgreSQL 中为 `ping_x_x_x_x`）存储 ping 结果（延迟、成功状态和时间戳）
- **告警表**：跟踪主机状态告警
- **恢复记录表**：记录主机从故障中恢复的信息

### SQLite vs PostgreSQL 表命名
- **SQLite**: `ip_10_224_1_11`
- **PostgreSQL**: `ping_10_224_1_11`

## 测试
- **单元测试**：包含针对 SQLite 和 PostgreSQL 的不同测试套件
- **测试文件**：
  - `test_sqlite_alerts.cpp`：SQLite 告警功能测试
  - `test_timezone.cpp`：时间戳处理功能测试
  - `test_alert_persistence.cpp`：告警持久化测试
  - `test_recovery_records.cpp`：恢复记录功能测试
  - `test_query_recovery.cpp`：恢复记录查询测试
  - `test_pg.cpp`：PostgreSQL 功能测试
- **启用测试构建**：使用 `-DBUILD_TESTS=ON` 选项编译测试程序
- **测试脚本**：
  - `test_alerts.sh`：告警功能测试脚本
  - `test_postgresql.sh`：PostgreSQL 功能测试脚本

### 运行测试
```bash
# 编译测试程序
cd build
cmake -DBUILD_TESTS=ON ..
make

# 运行 SQLite 测试
./test_sqlite_alerts
./test_timezone
./test_alert_persistence
./test_recovery_records
./test_query_recovery

# 运行 PostgreSQL 测试（需要启用 PostgreSQL 支持）
./test_pg

# 使用测试脚本
./test_alerts.sh
./test_postgresql.sh
```

## 开发实践
- **并发设计**：使用线程池和异步操作以提高性能
- **错误处理**：全面的异常处理和错误报告机制
- **资源管理**：使用智能指针进行自动内存管理
- **配置管理**：通过 ConfigManager 处理命令行参数和配置
- **数据库抽象**：通过 DatabaseInterface 抽象不同数据库后端
- **现代 C++ 特性**：使用 C++23 标准的新特性，如 `std::println` 用于格式化输出
- **时间处理**：所有时间戳都使用 UTC 时间以确保跨时区的一致性
- **工厂模式**：使用 DatabaseFactory 根据连接字符串自动检测数据库类型
- **线程安全**：使用互斥锁（`std::mutex`）和条件变量（`std::condition_variable`）确保线程安全

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
make

# 安装到系统（默认：/usr/local）
sudo make install

# 自定义安装路径
cmake -DCMAKE_INSTALL_PREFIX=/opt/mping ..
make
sudo make install
```

安装后，可使用 pkg-config 查询编译标志：
```bash
pkg-config --cflags --libs mping
```

## 版本信息
项目版本信息在 `project_info.h.in` 中定义，编译时生成 `project_info.h`：
- `PROJECT_VERSION`：主版本号
- `PROJECT_VERSION_MAJOR`：主版本号
- `PROJECT_VERSION_MINOR`：次版本号
- `PROJECT_VERSION_PATCH`：补丁版本号
- `PROJECT_DESCRIPTION`：项目描述
- `PROJECT_HOMEPAGE_URL`：项目主页
- `COMPILE_TIME`：编译时间戳