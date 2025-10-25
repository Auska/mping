# mping - Multi-host Ping Tool

mping is a command-line tool for checking the connectivity of multiple hosts simultaneously. It reads a list of IP addresses and hostnames from a file and performs ping operations on them concurrently.

## Features

- Concurrently ping multiple hosts for faster results
- Read hosts from a file with IP addresses and hostnames
- Display failed hosts by default
- Optionally display successful hosts with response time
- Configurable timeout for ping operations

## Usage

```bash
./mping [options] [filename]
```

### Options

- `-h`, `--help`: Show help message
- `-s`, `--show-success`: Show successful hosts (IP, hostname, delay)

### Default behavior

- Default filename: `ip.txt`
- Default behavior: Show failed hosts (IP, hostname)

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

```bash
mkdir build
cd build
cmake ..
make
```

## Example

```bash
# Show failed hosts
./mping ip.txt

# Show successful hosts with response time
./mping --show-success ip.txt
```