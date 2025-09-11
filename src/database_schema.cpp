#include "database_schema.hpp"

std::vector<std::string> DatabaseSchema::getCreateTableStatements() {
  return {// Users table
          R"(
        CREATE TABLE IF NOT EXISTS users (
            id VARCHAR(255) PRIMARY KEY,
            username VARCHAR(255) UNIQUE NOT NULL,
            email VARCHAR(255) UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            roles TEXT[] DEFAULT '{}',
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            is_active BOOLEAN DEFAULT TRUE
        );
        )",

          // Sessions table
          R"(
        CREATE TABLE IF NOT EXISTS sessions (
            session_id VARCHAR(255) PRIMARY KEY,
            user_id VARCHAR(255) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            expires_at TIMESTAMP WITH TIME ZONE NOT NULL,
            is_valid BOOLEAN DEFAULT TRUE
        );
        )",

          // ETL Jobs table
          R"(
        CREATE TABLE IF NOT EXISTS etl_jobs (
            job_id VARCHAR(255) PRIMARY KEY,
            job_type VARCHAR(50) NOT NULL CHECK (job_type IN ('EXTRACT', 'TRANSFORM', 'LOAD', 'FULL_ETL')),
            status VARCHAR(50) NOT NULL CHECK (status IN ('PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED')),
            source_config TEXT,
            target_config TEXT,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            started_at TIMESTAMP WITH TIME ZONE,
            completed_at TIMESTAMP WITH TIME ZONE,
            error_message TEXT,
            records_processed INTEGER DEFAULT 0,
            records_successful INTEGER DEFAULT 0,
            records_failed INTEGER DEFAULT 0,
            processing_rate DOUBLE PRECISION DEFAULT 0.0,
            memory_usage BIGINT DEFAULT 0,
            cpu_usage DOUBLE PRECISION DEFAULT 0.0,
            execution_time_ms BIGINT DEFAULT 0,
            peak_memory_usage BIGINT DEFAULT 0,
            peak_cpu_usage DOUBLE PRECISION DEFAULT 0.0,
            average_processing_rate DOUBLE PRECISION DEFAULT 0.0,
            total_bytes_processed BIGINT DEFAULT 0,
            total_bytes_written BIGINT DEFAULT 0,
            total_batches INTEGER DEFAULT 0,
            average_batch_size DOUBLE PRECISION DEFAULT 0.0,
            error_rate DOUBLE PRECISION DEFAULT 0.0,
            consecutive_errors INTEGER DEFAULT 0,
            time_to_first_error_ms BIGINT DEFAULT 0,
            throughput_mbps DOUBLE PRECISION DEFAULT 0.0,
            memory_efficiency DOUBLE PRECISION DEFAULT 0.0,
            cpu_efficiency DOUBLE PRECISION DEFAULT 0.0,
            start_time TIMESTAMP WITH TIME ZONE,
            last_update_time TIMESTAMP WITH TIME ZONE,
            first_error_time TIMESTAMP WITH TIME ZONE
        );
        )",

          // Job monitoring data table
          R"(
        CREATE TABLE IF NOT EXISTS job_monitoring (
            id SERIAL PRIMARY KEY,
            job_id VARCHAR(255) NOT NULL REFERENCES etl_jobs(job_id) ON DELETE CASCADE,
            progress_percent INTEGER DEFAULT 0,
            current_step TEXT,
            execution_time_ms BIGINT DEFAULT 0,
            recent_logs TEXT[],
            error_message TEXT,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
        );
        )",

          // Job logs table
          R"(
        CREATE TABLE IF NOT EXISTS job_logs (
            id SERIAL PRIMARY KEY,
            job_id VARCHAR(255) NOT NULL REFERENCES etl_jobs(job_id) ON DELETE CASCADE,
            level VARCHAR(20) NOT NULL,
            component VARCHAR(100),
            message TEXT NOT NULL,
            timestamp TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            context JSONB
        );
        )",

          // Configuration table (for dynamic configuration storage)
          R"(
        CREATE TABLE IF NOT EXISTS configuration (
            key VARCHAR(255) PRIMARY KEY,
            value TEXT NOT NULL,
            category VARCHAR(100),
            created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
        );
        )"};
}

std::vector<std::string> DatabaseSchema::getIndexStatements() {
  return {
      "CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);",
      "CREATE INDEX IF NOT EXISTS idx_sessions_expires_at ON "
      "sessions(expires_at);",
      "CREATE INDEX IF NOT EXISTS idx_etl_jobs_status ON etl_jobs(status);",
      "CREATE INDEX IF NOT EXISTS idx_etl_jobs_created_at ON "
      "etl_jobs(created_at);",
      "CREATE INDEX IF NOT EXISTS idx_etl_jobs_job_type ON etl_jobs(job_type);",
      "CREATE INDEX IF NOT EXISTS idx_job_monitoring_job_id ON "
      "job_monitoring(job_id);",
      "CREATE INDEX IF NOT EXISTS idx_job_logs_job_id ON job_logs(job_id);",
      "CREATE INDEX IF NOT EXISTS idx_job_logs_timestamp ON "
      "job_logs(timestamp);",
      "CREATE INDEX IF NOT EXISTS idx_job_logs_level ON job_logs(level);",
      "CREATE INDEX IF NOT EXISTS idx_configuration_category ON "
      "configuration(category);"};
}

std::vector<std::string> DatabaseSchema::getInitialDataStatements() {
  return {// Create default admin user
          R"(
        INSERT INTO users (id, username, email, password_hash, roles, is_active)
        VALUES ('admin-001', 'admin', 'admin@etlplus.com', '$2b$10$dummy.hash.for.admin', '{"admin", "user"}', true)
        ON CONFLICT (username) DO NOTHING;
        )"};
}
