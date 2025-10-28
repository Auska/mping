-- Create test database and user for mping PostgreSQL support
-- Run this script as a PostgreSQL superuser (e.g., postgres)

-- Create database
CREATE DATABASE mping_test;

-- Create user
CREATE USER mping_user WITH PASSWORD 'mping_pass';

-- Grant privileges
GRANT ALL PRIVILEGES ON DATABASE mping_test TO mping_user;