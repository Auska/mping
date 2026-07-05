# mping - Multi-host Ping Tool

mping is a command-line tool for checking the connectivity of multiple hosts simultaneously. It reads a list of IP addresses and hostnames from a file and performs ping operations on them concurrently. The tool also provides database logging and query capabilities to analyze ping results.

The project follows a command-pattern architecture with modular design:
- `main.cpp`: Argument parsing and command dispatch (~50 lines)
- `commands.h`/`commands.cpp`: Command abstraction and 5 sub-commands (QueryIP, Cleanup, QueryAlerts, QueryRecovery, Ping)
- `utils.cpp`/`utils.h`: Utility functions for file operations
- `ping_manager.cpp`/`ping_manager.h`: Core ping functionality (concurrent, thread pool)
- `database_manager.cpp`/`database_manager.h`: Database operations (SQLite)
- `database_manager_pg.cpp`/`database_manager_pg.h`: Database operations (PostgreSQL)
- `config_manager.cpp`/`config_manager.h`: Configuration management (XDG-compliant)
- `config_file.cpp`/`config_file.h`: INI-style config file parser
- `database_factory.cpp`/`database_factory.h`: Database factory pattern
- `database_interface.h`/`database_base.h`: Database abstraction layer

## Features

- Concurrently ping multiple hosts for faster results (thread pool, up to 50 concurrent)
- Read hosts from a file or database
- Display all hosts with status and response time
- Database logging of ping results with SQLite or PostgreSQL
- Query statistics for specific IP addresses
- Alert tracking with automatic recovery recording
- Config file support (XDG-compliant, INI format)
- Two-round ping strategy (1 fast packet, retry with 5 packets to reduce false positives)

## Usage

```bash
./mping [options] [filename]
```

### Options

- `-h`, `--help`: Show help message
- `-d`, `--database`: Enable database logging and specify database path/connection string
- `-f`, `--file`: Specify input file with hosts (default: ip.txt)
- `-q`, `--query`: Query statistics for a specific IP address (requires -d)
- `-a`, `--alerts [n]`: Query active alerts (requires -d, n: days, default: all)
- `-r`, `--recovery [n]`: Query recovery records (requires -d, n: days, default: all)
- `-C`, `--cleanup [n]`: Clean up data older than n days (requires -d, default: 30)
- `-s`, `--silent`: Silent mode, suppress output
- `-v`, `--version`: Show version information

### Default behavior

- Default filename: `ip.txt`
- Default behavior: Show all hosts with status (IP, hostname, status, delay)

### File format

The input file should contain lines in the following format:

```
# ip            hostname
10.224.1.11     test1
10.224.1.12     test2
```

Lines starting with `#` are treated as comments and ignored.

## Building

To build mping, you need:

- CMake 3.10 or higher
- A C++23 compatible compiler (GCC 13+, Clang 16+, MSVC 2022+)
- SQLite3 development libraries
- PostgreSQL development libraries (optional, for PostgreSQL support)

```bash
mkdir build
cd build
# For SQLite only support
cmake ..
make -j$(nproc)

# For PostgreSQL support
cmake -DUSE_POSTGRESQL=ON ..
make -j$(nproc)

# Build with tests
cmake -DBUILD_TESTS=ON ..
make -j$(nproc) mping_tests

# Build with sanitizers (Debug)
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON ..
make -j$(nproc)
```

55 test cases (506 assertions) with Catch2 v3:

```bash
./mping_tests
./mping_tests "[commands]"   # Run command pattern tests only
./mping_tests --list-tests   # List all test cases
```

## Example

```bash
# Ping all hosts in ip.txt with SQLite database logging
./mping -d ping_monitor.db

# Ping all hosts in silent mode
./mping -d ping_monitor.db -s

# Query statistics for a specific IP
./mping -d ping_monitor.db -q 10.224.1.11

# Query all active alerts
./mping -d ping_monitor.db -a

# Query alerts within the last 7 days
./mping -d ping_monitor.db -a 7

# Query alerts within the last 30 days with PostgreSQL
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -a 30

# Clean up data older than 30 days (default)
./mping -d ping_monitor.db -C

# Clean up data older than 60 days
./mping -d ping_monitor.db -C 60

# Use a different database path
./mping -d /path/to/mydb.db

# Use a different input file
./mping -d ping_monitor.db -f my_hosts.txt

# Use PostgreSQL database (auto-detected from connection string)
./mping -d "host=localhost user=myuser password=mypass dbname=mydb"

# Use PostgreSQL database and suppress NOTICE messages
./mping -d "host=localhost user=myuser password=mypass dbname=mydb client_min_messages=warning"

# Query statistics for a specific IP with PostgreSQL
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -q 10.224.1.11
```



## Database Schema

The tool creates four tables:

1. `hosts` table: Stores IP addresses and hostnames with creation and last seen timestamps
2. `ping_results` table: Unified table storing all ping results (IP, hostname, delay, success status, timestamp)
3. `alerts` table: Tracks host down-alerts with creation time
4. `recovery_records` table: Records when hosts recover from alert state

