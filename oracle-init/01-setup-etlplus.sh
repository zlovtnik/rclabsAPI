#!/bin/bash
# Oracle database initialization script for ETL Plus

echo "Setting up ETL Plus database schema..."

# Create ETL Plus user and schema
sqlplus sys/etlplus123@localhost:1521/FREE as sysdba <<EOF
-- Create tablespace for ETL Plus
CREATE TABLESPACE etlplus_tbs
DATAFILE '/opt/oracle/oradata/FREE/etlplus_tbs01.dbf'
SIZE 100M
AUTOEXTEND ON
NEXT 10M
MAXSIZE 1G;

-- Create ETL Plus user
CREATE USER etlplus IDENTIFIED BY etlplus123
DEFAULT TABLESPACE etlplus_tbs
TEMPORARY TABLESPACE temp
QUOTA UNLIMITED ON etlplus_tbs;

-- Grant necessary privileges
GRANT CONNECT, RESOURCE, DBA TO etlplus;
GRANT CREATE SESSION TO etlplus;
GRANT CREATE TABLE TO etlplus;
GRANT CREATE VIEW TO etlplus;
GRANT CREATE PROCEDURE TO etlplus;
GRANT CREATE SEQUENCE TO etlplus;

-- Connect as etlplus user and create basic tables
CONNECT etlplus/etlplus123@localhost:1521/FREE;

-- Create ETL Jobs table
CREATE TABLE etl_jobs (
    job_id NUMBER(10) PRIMARY KEY,
    job_name VARCHAR2(255) NOT NULL,
    job_status VARCHAR2(50) DEFAULT 'PENDING',
    source_config CLOB,
    target_config CLOB,
    transformation_rules CLOB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMP,
    completed_at TIMESTAMP
);

-- Create sequence for job IDs
CREATE SEQUENCE etl_jobs_seq
START WITH 1
INCREMENT BY 1
NOCACHE;

-- Create trigger for auto-incrementing job_id
CREATE OR REPLACE TRIGGER etl_jobs_trigger
BEFORE INSERT ON etl_jobs
FOR EACH ROW
BEGIN
    IF :NEW.job_id IS NULL THEN
        :NEW.job_id := etl_jobs_seq.NEXTVAL;
    END IF;
END;
/

-- Create ETL Job Logs table
CREATE TABLE etl_job_logs (
    log_id NUMBER(10) PRIMARY KEY,
    job_id NUMBER(10) REFERENCES etl_jobs(job_id),
    log_level VARCHAR2(20),
    message CLOB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create sequence for log IDs
CREATE SEQUENCE etl_job_logs_seq
START WITH 1
INCREMENT BY 1
NOCACHE;

-- Create trigger for auto-incrementing log_id
CREATE OR REPLACE TRIGGER etl_job_logs_trigger
BEFORE INSERT ON etl_job_logs
FOR EACH ROW
BEGIN
    IF :NEW.log_id IS NULL THEN
        :NEW.log_id := etl_job_logs_seq.NEXTVAL;
    END IF;
END;
/

-- Create Users table for authentication
CREATE TABLE users (
    user_id NUMBER(10) PRIMARY KEY,
    username VARCHAR2(255) UNIQUE NOT NULL,
    password_hash VARCHAR2(512) NOT NULL,
    email VARCHAR2(255),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP
);

-- Create sequence for user IDs
CREATE SEQUENCE users_seq
START WITH 1
INCREMENT BY 1
NOCACHE;

-- Create trigger for auto-incrementing user_id
CREATE OR REPLACE TRIGGER users_trigger
BEFORE INSERT ON users
FOR EACH ROW
BEGIN
    IF :NEW.user_id IS NULL THEN
        :NEW.user_id := users_seq.NEXTVAL;
    END IF;
END;
/

-- Insert default admin user (password: admin123)
INSERT INTO users (username, password_hash, email) 
VALUES ('admin', 'hashed_password_placeholder', 'admin@etlplus.com');

COMMIT;

SHOW USER;
SELECT 'ETL Plus Oracle database setup completed successfully!' FROM dual;
EXIT;
EOF

echo "Oracle database initialization completed!"
