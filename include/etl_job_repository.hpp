#pragma once

#include "etl_job_manager.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class DatabaseManager;

class ETLJobRepository {
public:
  explicit ETLJobRepository(std::shared_ptr<DatabaseManager> dbManager);

  // CRUD operations
  bool createJob(const ETLJob &job);
  std::optional<ETLJob> getJobById(const std::string &jobId);
  std::vector<ETLJob> getAllJobs();
  std::vector<ETLJob> getJobsByStatus(JobStatus status);
  bool updateJob(const ETLJob &job);
  bool deleteJob(const std::string &jobId);

  // Additional operations
  std::vector<ETLJob> getJobsByType(JobType type);
  std::vector<ETLJob> getActiveJobs();

private:
  std::shared_ptr<DatabaseManager> dbManager_;

  ETLJob jobFromRow(const std::vector<std::string> &row);
  std::string jobStatusToString(JobStatus status);
  JobStatus stringToJobStatus(const std::string &str);
  std::string jobTypeToString(JobType type);
  JobType stringToJobType(const std::string &str);
  std::string
  timePointToString(const std::chrono::system_clock::time_point &tp);
  std::chrono::system_clock::time_point
  stringToTimePoint(const std::string &str);
};
