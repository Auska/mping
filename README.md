# mping - Multi-host Ping Tool

mping is a command-line tool for checking the connectivity of multiple hosts simultaneously. It reads a list of IP addresses and hostnames from a file and performs ping operations on them concurrently. The tool also provides database logging and query capabilities to analyze ping results.

The project follows a command-pattern architecture with modular design:

- `main.cpp`: Argument parsing and command dispatch (~50 lines)
- `commands.h`/`commands.cpp`: Command abstraction and 5 sub-commands (QueryIP, Cleanup, QueryAlerts, QueryRecovery, Ping)
- `utils.cpp`/`utils.h`: Utility functions for file operations
- `ping_manager.cpp`/`ping_manager.h`: Core ping functionality (concurrent, thread pool)
- `database_manager_pg.cpp`/`database_manager_pg.h`: Database operations (PostgreSQL)
- `config_manager.cpp`/`config_manager.h`: Configuration management
- `config_file.cpp`/`config_file.h`: INI-style config file parser

## Features

- Concurrently ping multiple hosts for faster results (thread pool, up to 50 concurrent)
- Read hosts from a file or database
- Display all hosts with status and response time
- Database logging of ping results (PostgreSQL)
- Query statistics for specific IP addresses
- Alert tracking with automatic recovery recording
- Config file support (INI format, default: `~/.config/mping/config.json`)
- Two-round ping strategy (1 fast packet, retry with 5 packets to reduce false positives)
- Pre-alert confirmation: first-time failures are retried 3 times before an alert is raised (avoids false alerts from transient failures)

## Usage

```bash
./mping [options] [filename]
```

### Options

- `-h`: Show help message
- `-v`: Show version information
- `-d <connstr>`: Enable database logging and specify PostgreSQL connection string (libpq format)
- `-f <file>`: Specify input file with hosts (default: ip.txt)
- `-q <ip>`: Query statistics for a specific IP address (requires -d)
- `-a [n]`: Query active alerts (requires -d, n: days, default: all)
- `-r [n]`: Query recovery records (requires -d, n: days, default: all)
- `-C [n]`: Clean up data older than n days (requires -d, default: 30)
- `-s`: Silent mode, suppress output
- `-c <path>`: Load configuration from specified file
- `-N`: Do not load configuration file
- `-S [path]`: Save current configuration to file (default: `$HOME/.config/mping/config.json`)

### Default behavior

- Default filename: `ip.txt`
- Default behavior: Show all hosts with status (IP, hostname, status, delay)

### Raw socket mode (requires root or cap_net_raw)

mping sends ICMP echo requests through raw sockets, which require root privileges
or the `cap_net_raw` capability. If the capability is missing the socket cannot be
opened and every host is reported as unreachable. To enable unprivileged use
without full root, grant the capability once:

```bash
sudo setcap cap_net_raw+ep $(which mping)
```

Note: `setcap` is reset by reinstallation or rebuilds — re-apply after `cmake --install`.

### File format

The input file should contain lines in the following format:

```
# ip            hostname
10.224.1.11     test1
10.224.1.12     test2
```

Lines starting with `#` are treated as comments and ignored.

## Building

mping supports POSIX platforms only (Linux, macOS, BSD) — Windows is not supported.

To build mping, you need:

- CMake 3.16+
- A C++23 compatible compiler (GCC 13+, Clang 16+)
- PostgreSQL development libraries (libpq)
- Catch2 v3 (optional, for building tests)

```bash
# Configure and build (PostgreSQL is the only database backend)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Build with tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMPING_BUILD_TESTS=ON
cmake --build build -j

# Run all tests (via CTest)
ctest --test-dir build

# Run specific tests
./build/mping_tests "[commands]"
./build/mping_tests --list-tests
```

## Example

```bash
# Ping all hosts in ip.txt with PostgreSQL database logging
./mping -d "host=localhost user=myuser password=mypass dbname=mydb"

# Ping all hosts in silent mode
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -s

# Query statistics for a specific IP
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -q 10.224.1.11

# Query all active alerts
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -a

# Query alerts within the last 7 days
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -a7

# Clean up data older than 30 days (default)
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -C

# Clean up data older than 60 days
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -C60

# Use a different input file
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -f my_hosts.txt

# Use PostgreSQL database
./mping -d "host=localhost user=myuser password=mypass dbname=mydb"

# Suppress NOTICE messages
./mping -d "host=localhost user=myuser password=mypass dbname=mydb client_min_messages=warning"
```

## Database Schema

The tool creates five tables:

1. `hosts` table: Stores IP addresses and hostnames with creation and last seen timestamps
2. `ping_results` table: Unified table storing all ping results (IP, hostname, delay, success status, timestamp)
3. `alerts` table: Tracks host down-alerts with creation time
4. `recovery_records` table: Records when hosts recover from alert state
5. `mping_meta` table: Internal metadata (e.g. last auto-cleanup timestamp, throttled to once per 24h)
