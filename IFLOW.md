# mping - Multi-host Ping Tool 开发规范

## 项目概述
mping是一个命令行工具，用于同时检查多个主机的连接性。它从文件中读取IP地址和主机名列表，并并发执行ping操作。该工具还提供数据库日志记录和查询功能以分析ping结果。

## 架构设计
- **模块化设计**：分离关注点，各模块职责明确
- **`main.cpp`**：命令行界面和参数解析
- **`utils.cpp`/`utils.h`**：文件操作等实用函数
- **`ping_manager.cpp`/`ping_manager.h`**：核心ping功能实现
- **`database_manager.cpp`/`database_manager.h`**：SQLite数据库操作
- **`database_manager_pg.cpp`/`database_manager_pg.h`**：PostgreSQL数据库操作
- **`config_manager.cpp`/`config_manager.h`**：配置管理
- **`database_factory.cpp`/`database_factory.h`**：数据库工厂模式实现
- **`version_info.cpp`/`version_info.h`**：版本信息管理

## 编程规范
1. **使用C++23标准实现**：利用现代C++特性和性能优化
2. **commit必须使用英文**：提交信息应清晰描述变更内容
3. **使用面向对象方式实现**：采用封装、继承、多态等OOP原则
4. **优先使用项目中存在的函数**：避免重复造轮子，重用现有代码
5. **遵循工厂模式**：使用DatabaseFactory创建数据库实例
6. **使用现代C++特性**：如`std::println`、智能指针、移动语义等

## 项目特性
- 并发ping多个主机以获得更快的结果
- 从文件读取IP地址和主机名
- 显示所有主机的状态和响应时间
- 支持SQLite或PostgreSQL数据库日志记录
- 为特定IP地址查询统计信息
- 可配置的ping操作超时时间（默认3秒）
- 可配置的ping包数量（默认3个包）
- 主机状态监控和告警功能
- 恢复记录跟踪
- 数据自动清理功能
- 线程池优化的并发实现（默认最大并发数50）
- 时区处理和时间戳记录功能（所有写入数据库的时间都使用UTC时间）

## 构建系统
- **CMake 3.10+**：使用CMake作为构建系统
- **默认C++23标准**：`CMAKE_CXX_STANDARD 23`
- **支持构建类型**：Release（默认）和Debug
- **编译选项**：
  - `-DUSE_POSTGRESQL=ON`：启用PostgreSQL支持
  - `-DBUILD_TESTS=ON`：编译测试程序

### 构建命令
```bash
# 创建构建目录
mkdir build && cd build

# 仅启用SQLite（默认）
cmake ..
make

# 启用PostgreSQL支持
cmake -DUSE_POSTGRESQL=ON ..
make

# 调试版本构建
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# 使用构建脚本（支持Debug/Release）
./build.sh [Debug|Release]
```

## 依赖项
- **CMake 3.10或更高版本**
- **C++23兼容编译器**
- **SQLite3开发库**（必需）
- **PostgreSQL开发库**（可选，用于PostgreSQL支持）
- **线程库**（pthread，用于并发处理）

## 命令行选项
- `-h`, `--help`: 显示帮助信息
- `-v`, `--version`: 显示版本信息
- `-d`, `--database`: 启用数据库日志记录并指定数据库路径/连接字符串
- `-f`, `--file`: 指定包含主机的输入文件（默认：ip.txt）
- `-q`, `--query`: 查询特定IP地址的统计信息（需要-d）
- `-a`, `--alerts [n]`: 查询活动告警（需要-d，n: 天数，默认：全部）
- `-r`, `--recovery [n]`: 查询恢复记录（需要-d，n: 天数，默认：全部）
- `-C`, `--cleanup [n]`: 清理n天前的数据（需要-d，默认：30天）
- `-s`, `--silent`: 静默模式，抑制输出
- `-n`, `--count <n>`: 每个主机发送的ping包数量（默认：3）
- `-t`, `--timeout <n>`: 每个ping的超时时间（秒，默认：3）
- `-P`, `--postgresql`: 使用PostgreSQL数据库（需要-d与连接字符串）

## 数据库架构
- **`hosts`表**：存储IP地址和主机名及创建和最后访问时间戳
- **IP特定表**：每个IP有自己的表（SQLite中为`ip_x_x_x_x`，PostgreSQL中为`ping_x_x_x_x`）存储ping结果（延迟、成功状态和时间戳）
- **告警表**：跟踪主机状态告警
- **恢复记录表**：记录主机从故障中恢复的信息

## 测试
- **单元测试**：包含针对SQLite和PostgreSQL的不同测试套件
- **测试文件**：`test_*.cpp` 文件包含各种功能的测试用例
- **启用测试构建**：使用`-DBUILD_TESTS=ON`选项编译测试程序
- **时区测试**：`test_timezone.cpp` 测试时间戳处理功能

## 开发实践
- **并发设计**：使用线程池和异步操作以提高性能
- **错误处理**：全面的异常处理和错误报告机制
- **资源管理**：使用智能指针进行自动内存管理
- **配置管理**：通过ConfigManager处理命令行参数和配置
- **数据库抽象**：通过DatabaseInterface抽象不同数据库后端
- **现代C++特性**：使用C++23标准的新特性，如`std::println`用于格式化输出
- **时间处理**：所有时间戳都使用UTC时间以确保跨时区的一致性