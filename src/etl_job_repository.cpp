#ifdef ETL_ENABLE_POSTGRESQL
#include "etl_job_repository.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
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
                        "etl_jobs WHERE job_id = $1";
    std::vector<std::string> params = {jobId};

    auto result = dbManager_->selectQuery(query, params);
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
                        "etl_jobs WHERE status = $1 ORDER BY created_at DESC";
    std::vector<std::string> params = {statusStr};

    auto result = dbManager_->selectQuery(query, params);
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
    std::string query = "DELETE FROM etl_jobs WHERE job_id = $1";
    std::vector<std::string> params = {jobId};
    return dbManager_->executeQuery(query, params);
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
  constexpr size_t EXPECTED_COLUMN_COUNT =
      32; // All fields including timestamps
  if (row.size() < EXPECTED_COLUMN_COUNT) {
    throw std::invalid_argument("Invalid job row data: expected " +
                                std::to_string(EXPECTED_COLUMN_COUNT) +
                                " columns, got " + std::to_string(row.size()));
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

  // Safe conversions with error handling
  try {
    job.recordsProcessed = std::stoi(row[9]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid recordsProcessed value: " + row[9] +
                 ", defaulting to 0");
    job.recordsProcessed = 0;
  }

  try {
    job.recordsSuccessful = std::stoi(row[10]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid recordsSuccessful value: " + row[10] +
                 ", defaulting to 0");
    job.recordsSuccessful = 0;
  }

  try {
    job.recordsFailed = std::stoi(row[11]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid recordsFailed value: " + row[11] +
                 ", defaulting to 0");
    job.recordsFailed = 0;
  }

  // Parse metrics with safe conversions
  try {
    job.metrics.processingRate = std::stod(row[12]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid processingRate value: " + row[12] +
                 ", defaulting to 0.0");
    job.metrics.processingRate = 0.0;
  }

  try {
    job.metrics.memoryUsage = std::stoul(row[13]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid memoryUsage value: " + row[13] + ", defaulting to 0");
    job.metrics.memoryUsage = 0;
  }

  try {
    job.metrics.cpuUsage = std::stod(row[14]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid cpuUsage value: " + row[14] + ", defaulting to 0.0");
    job.metrics.cpuUsage = 0.0;
  }

  try {
    job.metrics.executionTime = std::chrono::milliseconds(std::stoll(row[15]));
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid executionTime value: " + row[15] +
                 ", defaulting to 0ms");
    job.metrics.executionTime = std::chrono::milliseconds(0);
  }

  try {
    job.metrics.peakMemoryUsage = std::stoul(row[16]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid peakMemoryUsage value: " + row[16] +
                 ", defaulting to 0");
    job.metrics.peakMemoryUsage = 0;
  }

  try {
    job.metrics.peakCpuUsage = std::stod(row[17]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid peakCpuUsage value: " + row[17] +
                 ", defaulting to 0.0");
    job.metrics.peakCpuUsage = 0.0;
  }

  try {
    job.metrics.averageProcessingRate = std::stod(row[18]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid averageProcessingRate value: " + row[18] +
                 ", defaulting to 0.0");
    job.metrics.averageProcessingRate = 0.0;
  }

  try {
    job.metrics.totalBytesProcessed = std::stoul(row[19]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid totalBytesProcessed value: " + row[19] +
                 ", defaulting to 0");
    job.metrics.totalBytesProcessed = 0;
  }

  try {
    job.metrics.totalBytesWritten = std::stoul(row[20]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid totalBytesWritten value: " + row[20] +
                 ", defaulting to 0");
    job.metrics.totalBytesWritten = 0;
  }

  try {
    job.metrics.totalBatches = std::stoi(row[21]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid totalBatches value: " + row[21] +
                 ", defaulting to 0");
    job.metrics.totalBatches = 0;
  }

  try {
    job.metrics.averageBatchSize = std::stod(row[22]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid averageBatchSize value: " + row[22] +
                 ", defaulting to 0.0");
    job.metrics.averageBatchSize = 0.0;
  }

  try {
    job.metrics.errorRate = std::stod(row[23]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid errorRate value: " + row[23] + ", defaulting to 0.0");
    job.metrics.errorRate = 0.0;
  }

  try {
    job.metrics.consecutiveErrors = std::stoi(row[24]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid consecutiveErrors value: " + row[24] +
                 ", defaulting to 0");
    job.metrics.consecutiveErrors = 0;
  }

  try {
    job.metrics.timeToFirstError =
        std::chrono::milliseconds(std::stoll(row[25]));
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid timeToFirstError value: " + row[25] +
                 ", defaulting to 0ms");
    job.metrics.timeToFirstError = std::chrono::milliseconds(0);
  }

  try {
    job.metrics.throughputMBps = std::stod(row[26]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid throughputMBps value: " + row[26] +
                 ", defaulting to 0.0");
    job.metrics.throughputMBps = 0.0;
  }

  try {
    job.metrics.memoryEfficiency = std::stod(row[27]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid memoryEfficiency value: " + row[27] +
                 ", defaulting to 0.0");
    job.metrics.memoryEfficiency = 0.0;
  }

  try {
    job.metrics.cpuEfficiency = std::stod(row[28]);
  } catch (const std::exception &e) {
    ETL_LOG_WARN("Invalid cpuEfficiency value: " + row[28] +
                 ", defaulting to 0.0");
    job.metrics.cpuEfficiency = 0.0;
  }

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

  ETL_LOG_WARN("Unknown job status string: " + str + ", defaulting to PENDING");
  return JobStatus::PENDING; // Default with warning
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
  return std::chrono::system_clock::from_time_t(timegm(&tm));
}
#endif
