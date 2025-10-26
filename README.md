# mping - Multi-host Ping Tool

mping is a command-line tool for checking the connectivity of multiple hosts simultaneously. It reads a list of IP addresses and hostnames from a file and performs ping operations on them concurrently. The tool also provides database logging and query capabilities to analyze ping results.

The project follows a modular design with separated concerns:
- `main.cpp`: Command-line interface and argument parsing
- `utils.cpp`/`utils.h`: Utility functions for file operations
- `ping.cpp`/`ping.h`: Core ping functionality
- `db.cpp`/`db.h`: Database operations
- `print_results.cpp`/`print_results.h`: Output formatting and display

## Features

- Concurrently ping multiple hosts for faster results
- Read hosts from a file with IP addresses and hostnames
- Display all hosts with status and response time
- Database logging of ping results with SQLite
- Query statistics for specific IP addresses
- Query hosts with consecutive failures
- Configurable timeout for ping operations

## Usage

```bash
./mping [options] [filename]
```

### Options

- `-h`, `--help`: Show help message
- `-d`, `--database`: Enable database logging and specify database path
- `-f`, `--file`: Specify input file with hosts (default: ip.txt)
- `-q`, `--query`: Query statistics for a specific IP address (requires -d)
- `-c`, `--consecutive-failures [n]`: Query hosts with n consecutive failures (requires -d)
- `-C`, `--cleanup [n]`: Clean up data older than n days (requires -d, default: 30)
- `-s`, `--silent`: Silent mode, suppress output

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
- A C++20 compatible compiler
- SQLite3 development libraries

```bash
mkdir build
cd build
cmake ..
make
```

The project consists of the following source files:
- `main.cpp`: Main entry point and command-line argument handling
- `utils.cpp`/`utils.h`: Utility functions for reading hosts from file
- `ping.cpp`/`ping.h`: Core ping functionality with concurrent execution
- `db.cpp`/`db.h`: Database operations for storing and querying results
- `print_results.cpp`/`print_results.h`: Functions for displaying results and help

## Example

```bash
# Ping all hosts in ip.txt with database logging
./mping -d ping_monitor.db

# Ping all hosts in silent mode
./mping -d ping_monitor.db -s

# Query statistics for a specific IP
./mping -d ping_monitor.db -q 10.224.1.11

# Query hosts with 3 consecutive failures
./mping -d ping_monitor.db -c

# Query hosts with 5 consecutive failures
./mping -d ping_monitor.db -c 5

# Clean up data older than 30 days (default)
./mping -d ping_monitor.db -C

# Clean up data older than 60 days
./mping -d ping_monitor.db -C 60

# Use a different database path
./mping -d /path/to/mydb.db -c

# Use a different input file
./mping -d ping_monitor.db -f my_hosts.txt
```

## Database Schema

The tool creates two types of tables:

1. `hosts` table: Stores IP addresses and hostnames with creation and last seen timestamps
2. IP-specific tables: Each IP gets its own table (e.g., `ip_10_224_1_11`) to store ping results with delay, success status, and timestamp.