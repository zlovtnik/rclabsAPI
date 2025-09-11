#include "etl_job_repository.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <pqxx/pqxx>
#include <sstream>

ETLJobRepository::ETLJobRepository(std::shared_ptr<DatabaseManager> dbManager)
    : dbManager_(dbManager) {}

bool ETLJobRepository::createJob(const ETLJob &job) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string statusStr = jobStatusToString(job.status);
    std::string typeStr = jobTypeToString(job.type);
    std::string createdAtStr = timePointToString(job.createdAt);
    std::string startedAtStr = job.startedAt.time_since_epoch().count() > 0
                                   ? timePointToString(job.startedAt)
                                   : "NULL";
    std::string completedAtStr = job.completedAt.time_since_epoch().count() > 0
                                     ? timePointToString(job.completedAt)
                                     : "NULL";

    std::string query =
        "INSERT INTO etl_jobs (job_id, job_type, status, source_config, "
        "target_config, "
        "created_at, started_at, completed_at, error_message, "
        "records_processed, "
        "records_successful, records_failed, processing_rate, memory_usage, "
        "cpu_usage, "
        "execution_time_ms, peak_memory_usage, peak_cpu_usage, "
        "average_processing_rate, "
        "total_bytes_processed, total_bytes_written, total_batches, "
        "average_batch_size, "
        "error_rate, consecutive_errors, time_to_first_error_ms, "
        "throughput_mbps, "
        "memory_efficiency, cpu_efficiency, start_time, last_update_time, "
        "first_error_time) "
        "VALUES ('" +
        job.jobId + "', '" + typeStr + "', '" + statusStr + "', '" +
        job.sourceConfig + "', '" + job.targetConfig + "', '" + createdAtStr +
        "', " + (startedAtStr != "NULL" ? "'" + startedAtStr + "'" : "NULL") +
        ", " +
        (completedAtStr != "NULL" ? "'" + completedAtStr + "'" : "NULL") +
        ", " +
        (job.errorMessage.empty() ? "NULL" : "'" + job.errorMessage + "'") +
        ", " + std::to_string(job.recordsProcessed) + ", " +
        std::to_string(job.recordsSuccessful) + ", " +
        std::to_string(job.recordsFailed) + ", " +
        std::to_string(job.metrics.processingRate) + ", " +
        std::to_string(job.metrics.memoryUsage) + ", " +
        std::to_string(job.metrics.cpuUsage) + ", " +
        std::to_string(job.metrics.executionTime.count()) + ", " +
        std::to_string(job.metrics.peakMemoryUsage) + ", " +
        std::to_string(job.metrics.peakCpuUsage) + ", " +
        std::to_string(job.metrics.averageProcessingRate) + ", " +
        std::to_string(job.metrics.totalBytesProcessed) + ", " +
        std::to_string(job.metrics.totalBytesWritten) + ", " +
        std::to_string(job.metrics.totalBatches) + ", " +
        std::to_string(job.metrics.averageBatchSize) + ", " +
        std::to_string(job.metrics.errorRate) + ", " +
        std::to_string(job.metrics.consecutiveErrors) + ", " +
        std::to_string(job.metrics.timeToFirstError.count()) + ", " +
        std::to_string(job.metrics.throughputMBps) + ", " +
        std::to_string(job.metrics.memoryEfficiency) + ", " +
        std::to_string(job.metrics.cpuEfficiency) + ", " +
        (job.metrics.startTime.time_since_epoch().count() > 0
             ? "'" + timePointToString(job.metrics.startTime) + "'"
             : "NULL") +
        ", " +
        (job.metrics.lastUpdateTime.time_since_epoch().count() > 0
             ? "'" + timePointToString(job.metrics.lastUpdateTime) + "'"
             : "NULL") +
        ", " +
        (job.metrics.firstErrorTime.time_since_epoch().count() > 0
             ? "'" + timePointToString(job.metrics.firstErrorTime) + "'"
             : "NULL") +
        ")";

    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to create job: " + std::string(e.what()));
    return false;
  }
}

std::optional<ETLJob> ETLJobRepository::getJobById(const std::string &jobId) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return std::nullopt;
  }

  try {
    std::string query = "SELECT job_id, job_type, status, source_config, "
                        "target_config, created_at, "
                        "started_at, completed_at, error_message, "
                        "records_processed, records_successful, "
                        "records_failed, processing_rate, memory_usage, "
                        "cpu_usage, execution_time_ms, "
                        "peak_memory_usage, peak_cpu_usage, "
                        "average_processing_rate, total_bytes_processed, "
                        "total_bytes_written, total_batches, "
                        "average_batch_size, error_rate, consecutive_errors, "
                        "time_to_first_error_ms, throughput_mbps, "
                        "memory_efficiency, cpu_efficiency, "
                        "start_time, last_update_time, first_error_time FROM "
                        "etl_jobs WHERE job_id = '" +
                        jobId + "'";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return std::nullopt;
    }

    return jobFromRow(result[1]);
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to get job by ID: " + std::string(e.what()));
    return std::nullopt;
  }
}

std::vector<ETLJob> ETLJobRepository::getAllJobs() {
  std::vector<ETLJob> jobs;

  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return jobs;
  }

  try {
    std::string query = "SELECT job_id, job_type, status, source_config, "
                        "target_config, created_at, "
                        "started_at, completed_at, error_message, "
                        "records_processed, records_successful, "
                        "records_failed, processing_rate, memory_usage, "
                        "cpu_usage, execution_time_ms, "
                        "peak_memory_usage, peak_cpu_usage, "
                        "average_processing_rate, total_bytes_processed, "
                        "total_bytes_written, total_batches, "
                        "average_batch_size, error_rate, consecutive_errors, "
                        "time_to_first_error_ms, throughput_mbps, "
                        "memory_efficiency, cpu_efficiency, "
                        "start_time, last_update_time, first_error_time FROM "
                        "etl_jobs ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return jobs;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      ETLJob job = jobFromRow(result[i]);
      jobs.push_back(job);
    }
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to get all jobs: " + std::string(e.what()));
  }

  return jobs;
}

std::vector<ETLJob> ETLJobRepository::getJobsByStatus(JobStatus status) {
  std::vector<ETLJob> jobs;

  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return jobs;
  }

  try {
    std::string statusStr = jobStatusToString(status);
    std::string query = "SELECT job_id, job_type, status, source_config, "
                        "target_config, created_at, "
                        "started_at, completed_at, error_message, "
                        "records_processed, records_successful, "
                        "records_failed, processing_rate, memory_usage, "
                        "cpu_usage, execution_time_ms, "
                        "peak_memory_usage, peak_cpu_usage, "
                        "average_processing_rate, total_bytes_processed, "
                        "total_bytes_written, total_batches, "
                        "average_batch_size, error_rate, consecutive_errors, "
                        "time_to_first_error_ms, throughput_mbps, "
                        "memory_efficiency, cpu_efficiency, "
                        "start_time, last_update_time, first_error_time FROM "
                        "etl_jobs WHERE status = '" +
                        statusStr + "' ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return jobs;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      ETLJob job = jobFromRow(result[i]);
      jobs.push_back(job);
    }
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to get jobs by status: " + std::string(e.what()));
  }

  return jobs;
}

bool ETLJobRepository::updateJob(const ETLJob &job) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string statusStr = jobStatusToString(job.status);
    std::string startedAtStr = job.startedAt.time_since_epoch().count() > 0
                                   ? timePointToString(job.startedAt)
                                   : "NULL";
    std::string completedAtStr = job.completedAt.time_since_epoch().count() > 0
                                     ? timePointToString(job.completedAt)
                                     : "NULL";

    std::string query =
        "UPDATE etl_jobs SET status = '" + statusStr + "', started_at = " +
        (startedAtStr != "NULL" ? "'" + startedAtStr + "'" : "NULL") +
        ", completed_at = " +
        (completedAtStr != "NULL" ? "'" + completedAtStr + "'" : "NULL") +
        ", error_message = " +
        (job.errorMessage.empty() ? "NULL" : "'" + job.errorMessage + "'") +
        ", records_processed = " + std::to_string(job.recordsProcessed) +
        ", records_successful = " + std::to_string(job.recordsSuccessful) +
        ", records_failed = " + std::to_string(job.recordsFailed) +
        ", processing_rate = " + std::to_string(job.metrics.processingRate) +
        ", memory_usage = " + std::to_string(job.metrics.memoryUsage) +
        ", cpu_usage = " + std::to_string(job.metrics.cpuUsage) +
        ", execution_time_ms = " +
        std::to_string(job.metrics.executionTime.count()) +
        ", peak_memory_usage = " + std::to_string(job.metrics.peakMemoryUsage) +
        ", peak_cpu_usage = " + std::to_string(job.metrics.peakCpuUsage) +
        ", average_processing_rate = " +
        std::to_string(job.metrics.averageProcessingRate) +
        ", total_bytes_processed = " +
        std::to_string(job.metrics.totalBytesProcessed) +
        ", total_bytes_written = " +
        std::to_string(job.metrics.totalBytesWritten) +
        ", total_batches = " + std::to_string(job.metrics.totalBatches) +
        ", average_batch_size = " +
        std::to_string(job.metrics.averageBatchSize) +
        ", error_rate = " + std::to_string(job.metrics.errorRate) +
        ", consecutive_errors = " +
        std::to_string(job.metrics.consecutiveErrors) +
        ", time_to_first_error_ms = " +
        std::to_string(job.metrics.timeToFirstError.count()) +
        ", throughput_mbps = " + std::to_string(job.metrics.throughputMBps) +
        ", memory_efficiency = " +
        std::to_string(job.metrics.memoryEfficiency) +
        ", cpu_efficiency = " + std::to_string(job.metrics.cpuEfficiency) +
        ", last_update_time = '" +
        timePointToString(std::chrono::system_clock::now()) +
        "' WHERE job_id = '" + job.jobId + "'";

    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to update job: " + std::string(e.what()));
    return false;
  }
}

bool ETLJobRepository::deleteJob(const std::string &jobId) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string query = "DELETE FROM etl_jobs WHERE job_id = '" + jobId + "'";
    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to delete job: " + std::string(e.what()));
    return false;
  }
}

std::vector<ETLJob> ETLJobRepository::getJobsByType(JobType type) {
  std::vector<ETLJob> jobs;

  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return jobs;
  }

  try {
    std::string typeStr = jobTypeToString(type);
    std::string query = "SELECT job_id, job_type, status, source_config, "
                        "target_config, created_at, "
                        "started_at, completed_at, error_message, "
                        "records_processed, records_successful, "
                        "records_failed, processing_rate, memory_usage, "
                        "cpu_usage, execution_time_ms, "
                        "peak_memory_usage, peak_cpu_usage, "
                        "average_processing_rate, total_bytes_processed, "
                        "total_bytes_written, total_batches, "
                        "average_batch_size, error_rate, consecutive_errors, "
                        "time_to_first_error_ms, throughput_mbps, "
                        "memory_efficiency, cpu_efficiency, "
                        "start_time, last_update_time, first_error_time FROM "
                        "etl_jobs WHERE job_type = '" +
                        typeStr + "' ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return jobs;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      ETLJob job = jobFromRow(result[i]);
      jobs.push_back(job);
    }
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to get jobs by type: " + std::string(e.what()));
  }

  return jobs;
}

std::vector<ETLJob> ETLJobRepository::getActiveJobs() {
  std::vector<ETLJob> jobs;

  if (!dbManager_ || !dbManager_->isConnected()) {
    ETL_LOG_ERROR("Database not connected");
    return jobs;
  }

  try {
    std::string query = "SELECT job_id, job_type, status, source_config, "
                        "target_config, created_at, "
                        "started_at, completed_at, error_message, "
                        "records_processed, records_successful, "
                        "records_failed, processing_rate, memory_usage, "
                        "cpu_usage, execution_time_ms, "
                        "peak_memory_usage, peak_cpu_usage, "
                        "average_processing_rate, total_bytes_processed, "
                        "total_bytes_written, total_batches, "
                        "average_batch_size, error_rate, consecutive_errors, "
                        "time_to_first_error_ms, throughput_mbps, "
                        "memory_efficiency, cpu_efficiency, "
                        "start_time, last_update_time, first_error_time FROM "
                        "etl_jobs WHERE status IN ('PENDING', 'RUNNING') "
                        "ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return jobs;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      ETLJob job = jobFromRow(result[i]);
      jobs.push_back(job);
    }
  } catch (const std::exception &e) {
    ETL_LOG_ERROR("Failed to get active jobs: " + std::string(e.what()));
  }

  return jobs;
}

ETLJob ETLJobRepository::jobFromRow(const std::vector<std::string> &row) {
  if (row.size() < 30) {
    throw std::runtime_error("Invalid job row data");
  }

  ETLJob job;
  job.jobId = row[0];
  job.type = stringToJobType(row[1]);
  job.status = stringToJobStatus(row[2]);
  job.sourceConfig = row[3];
  job.targetConfig = row[4];
  job.createdAt = stringToTimePoint(row[5]);

  if (!row[6].empty() && row[6] != "NULL") {
    job.startedAt = stringToTimePoint(row[6]);
  }
  if (!row[7].empty() && row[7] != "NULL") {
    job.completedAt = stringToTimePoint(row[7]);
  }

  job.errorMessage = row[8].empty() || row[8] == "NULL" ? "" : row[8];
  job.recordsProcessed = std::stoi(row[9]);
  job.recordsSuccessful = std::stoi(row[10]);
  job.recordsFailed = std::stoi(row[11]);

  // Parse metrics
  job.metrics.processingRate = std::stod(row[12]);
  job.metrics.memoryUsage = std::stoul(row[13]);
  job.metrics.cpuUsage = std::stod(row[14]);
  job.metrics.executionTime = std::chrono::milliseconds(std::stoll(row[15]));
  job.metrics.peakMemoryUsage = std::stoul(row[16]);
  job.metrics.peakCpuUsage = std::stod(row[17]);
  job.metrics.averageProcessingRate = std::stod(row[18]);
  job.metrics.totalBytesProcessed = std::stoul(row[19]);
  job.metrics.totalBytesWritten = std::stoul(row[20]);
  job.metrics.totalBatches = std::stoi(row[21]);
  job.metrics.averageBatchSize = std::stod(row[22]);
  job.metrics.errorRate = std::stod(row[23]);
  job.metrics.consecutiveErrors = std::stoi(row[24]);
  job.metrics.timeToFirstError = std::chrono::milliseconds(std::stoll(row[25]));
  job.metrics.throughputMBps = std::stod(row[26]);
  job.metrics.memoryEfficiency = std::stod(row[27]);
  job.metrics.cpuEfficiency = std::stod(row[28]);

  if (!row[29].empty() && row[29] != "NULL") {
    job.metrics.startTime = stringToTimePoint(row[29]);
  }
  if (!row[30].empty() && row[30] != "NULL") {
    job.metrics.lastUpdateTime = stringToTimePoint(row[30]);
  }
  if (!row[31].empty() && row[31] != "NULL") {
    job.metrics.firstErrorTime = stringToTimePoint(row[31]);
  }

  return job;
}

std::string ETLJobRepository::jobStatusToString(JobStatus status) {
  switch (status) {
  case JobStatus::PENDING:
    return "PENDING";
  case JobStatus::RUNNING:
    return "RUNNING";
  case JobStatus::COMPLETED:
    return "COMPLETED";
  case JobStatus::FAILED:
    return "FAILED";
  case JobStatus::CANCELLED:
    return "CANCELLED";
  default:
    return "UNKNOWN";
  }
}

JobStatus ETLJobRepository::stringToJobStatus(const std::string &str) {
  if (str == "PENDING")
    return JobStatus::PENDING;
  if (str == "RUNNING")
    return JobStatus::RUNNING;
  if (str == "COMPLETED")
    return JobStatus::COMPLETED;
  if (str == "FAILED")
    return JobStatus::FAILED;
  if (str == "CANCELLED")
    return JobStatus::CANCELLED;
  return JobStatus::PENDING; // Default
}

std::string ETLJobRepository::jobTypeToString(JobType type) {
  switch (type) {
  case JobType::EXTRACT:
    return "EXTRACT";
  case JobType::TRANSFORM:
    return "TRANSFORM";
  case JobType::LOAD:
    return "LOAD";
  case JobType::FULL_ETL:
    return "FULL_ETL";
  default:
    return "FULL_ETL";
  }
}

JobType ETLJobRepository::stringToJobType(const std::string &str) {
  if (str == "EXTRACT")
    return JobType::EXTRACT;
  if (str == "TRANSFORM")
    return JobType::TRANSFORM;
  if (str == "LOAD")
    return JobType::LOAD;
  if (str == "FULL_ETL")
    return JobType::FULL_ETL;
  return JobType::FULL_ETL; // Default
}

std::string ETLJobRepository::timePointToString(
    const std::chrono::system_clock::time_point &tp) {
  auto time = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::gmtime(&time);
  std::stringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

std::chrono::system_clock::time_point
ETLJobRepository::stringToTimePoint(const std::string &str) {
  std::tm tm = {};
  std::istringstream ss(str);
  ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  if (ss.fail()) {
    return std::chrono::system_clock::now();
  }
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}
