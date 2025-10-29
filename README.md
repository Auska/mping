# mping - Multi-host Ping Tool

mping is a command-line tool for checking the connectivity of multiple hosts simultaneously. It reads a list of IP addresses and hostnames from a file and performs ping operations on them concurrently. The tool also provides database logging and query capabilities to analyze ping results.

The project follows a modular design with separated concerns:
- `main.cpp`: Command-line interface and argument parsing
- `utils.cpp`/`utils.h`: Utility functions for file operations
- `ping_manager.cpp`/`ping_manager.h`: Core ping functionality
- `database_manager.cpp`/`database_manager.h`: Database operations (SQLite)
- `database_manager_pg.cpp`/`database_manager_pg.h`: Database operations (PostgreSQL)
- `config_manager.cpp`/`config_manager.h`: Configuration management

## Features

- Concurrently ping multiple hosts for faster results
- Read hosts from a file with IP addresses and hostnames
- Display all hosts with status and response time
- Database logging of ping results with SQLite or PostgreSQL
- Query statistics for specific IP addresses
- Configurable timeout for ping operations

## Usage

```bash
./mping [options] [filename]
```

### Options

- `-h`, `--help`: Show help message
- `-d`, `--database`: Enable database logging and specify database path/connection string
- `-f`, `--file`: Specify input file with hosts (default: ip.txt)
- `-q`, `--query`: Query statistics for a specific IP address (requires -d)
- `-C`, `--cleanup [n]`: Clean up data older than n days (requires -d, default: 30)
- `-s`, `--silent`: Silent mode, suppress output
- `-P`, `--postgresql`: Use PostgreSQL database (requires -d with connection string)

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
- PostgreSQL development libraries (optional, for PostgreSQL support)

```bash
mkdir build
cd build
# For SQLite only support
cmake ..
# For PostgreSQL support
cmake -DUSE_POSTGRESQL=ON ..
make
```

The project consists of the following source files:
- `main.cpp`: Main entry point and command-line argument handling
- `utils.cpp`/`utils.h`: Utility functions for reading hosts from file
- `ping_manager.cpp`/`ping_manager.h`: Core ping functionality with concurrent execution
- `database_manager.cpp`/`database_manager.h`: Database operations for storing and querying results (SQLite)
- `database_manager_pg.cpp`/`database_manager_pg.h`: Database operations for storing and querying results (PostgreSQL)
- `config_manager.cpp`/`config_manager.h`: Configuration management

## Example

```bash
# Ping all hosts in ip.txt with SQLite database logging
./mping -d ping_monitor.db

# Ping all hosts in silent mode
./mping -d ping_monitor.db -s

# Query statistics for a specific IP
./mping -d ping_monitor.db -q 10.224.1.11



# Clean up data older than 30 days (default)
./mping -d ping_monitor.db -C

# Clean up data older than 60 days
./mping -d ping_monitor.db -C 60

# Use a different database path
./mping -d /path/to/mydb.db

# Use a different input file
./mping -d ping_monitor.db -f my_hosts.txt

# Use PostgreSQL database
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -P

# Use PostgreSQL database and suppress NOTICE messages
./mping -d "host=localhost user=myuser password=mypass dbname=mydb client_min_messages=warning" -P

# Query statistics for a specific IP with PostgreSQL
./mping -d "host=localhost user=myuser password=mypass dbname=mydb" -P -q 10.224.1.11
```



## Database Schema

The tool creates two types of tables:

1. `hosts` table: Stores IP addresses and hostnames with creation and last seen timestamps
2. IP-specific tables: Each IP gets its own table (e.g., `ip_10_224_1_11` for SQLite or `ping_10_224_1_11` for PostgreSQL) to store ping results with delay, success status, and timestamp.

