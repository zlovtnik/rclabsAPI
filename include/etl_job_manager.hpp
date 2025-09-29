#pragma once

#include "etl_job_models.hpp"
#include "lock_utils.hpp"
#include "system_metrics.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Abstract interface for ETL job repository
class IETLJobRepository {
public:
  virtual ~IETLJobRepository() = default;

  // CRUD operations
  virtual bool createJob(const ETLJob &job) = 0;
  virtual std::optional<ETLJob> getJobById(const std::string &jobId) = 0;
  virtual std::vector<ETLJob> getAllJobs() = 0;
  virtual std::vector<ETLJob> getJobsByStatus(JobStatus status) = 0;
  virtual bool updateJob(const ETLJob &job) = 0;
  virtual bool deleteJob(const std::string &jobId) = 0;

  // Additional operations
  virtual std::vector<ETLJob> getJobsByType(JobType type) = 0;
  virtual std::vector<ETLJob> getActiveJobs() = 0;
};

// Concrete implementation that wraps the existing ETLJobRepository
#ifdef ETL_ENABLE_POSTGRESQL
class PostgresETLJobRepository : public IETLJobRepository {
public:
  explicit PostgresETLJobRepository(std::shared_ptr<DatabaseManager> dbManager);

  // CRUD operations
  bool createJob(const ETLJob &job) override;
  std::optional<ETLJob> getJobById(const std::string &jobId) override;
  std::vector<ETLJob> getAllJobs() override;
  std::vector<ETLJob> getJobsByStatus(JobStatus status) override;
  bool updateJob(const ETLJob &job) override;
  bool deleteJob(const std::string &jobId) override;

  // Additional operations
  std::vector<ETLJob> getJobsByType(JobType type) override;
  std::vector<ETLJob> getActiveJobs() override;

private:
  std::shared_ptr<ETLJobRepository> concreteRepo_;
};
#endif

// Null/in-memory implementation for non-DB builds
class NullETLJobRepository : public IETLJobRepository {
public:
  NullETLJobRepository() = default;

  // CRUD operations - all return success but do nothing
  bool createJob(const ETLJob &job) override {
    return true; // Pretend success
  }

  std::optional<ETLJob> getJobById(const std::string &jobId) override {
    return std::nullopt; // No jobs in memory
  }

  std::vector<ETLJob> getAllJobs() override {
    return {}; // No jobs
  }

  std::vector<ETLJob> getJobsByStatus(JobStatus status) override {
    return {}; // No jobs
  }

  bool updateJob(const ETLJob &job) override {
    return true; // Pretend success
  }

  bool deleteJob(const std::string &jobId) override {
    return true; // Pretend success
  }

  // Additional operations
  std::vector<ETLJob> getJobsByType(JobType type) override {
    return {}; // No jobs
  }

  std::vector<ETLJob> getActiveJobs() override {
    return {}; // No jobs
  }
};

class ETLJobManager {
public:
#ifdef ETL_ENABLE_POSTGRESQL
  ETLJobManager(std::shared_ptr<DatabaseManager> dbManager,
                std::shared_ptr<DataTransformer> transformer);
#endif
  ETLJobManager(std::shared_ptr<DataTransformer> transformer);
  ETLJobManager(std::shared_ptr<IETLJobRepository> jobRepo,
                std::shared_ptr<DataTransformer> transformer);
  ~ETLJobManager();

  // Job management
  std::string scheduleJob(const ETLJobConfig &config);
  bool cancelJob(const std::string &jobId);
  bool pauseJob(const std::string &jobId);
  bool resumeJob(const std::string &jobId);

  // Job monitoring
  std::shared_ptr<ETLJob> getJob(const std::string &jobId) const;
  std::vector<std::shared_ptr<ETLJob>> getAllJobs() const;
  std::vector<std::shared_ptr<ETLJob>> getJobsByStatus(JobStatus status) const;

  // Job execution
  void start();
  void stop();
  bool isRunning() const;

  // Job monitoring integration
  void
  setJobMonitorService(std::shared_ptr<JobMonitorServiceInterface> monitor);
  void publishJobStatusUpdate(const std::string &jobId, JobStatus status);
  void publishJobProgress(const std::string &jobId, int progress,
                          const std::string &step);
  void publishJobMetrics(const std::string &jobId, const JobMetrics &metrics);

  // Metrics collection management
  void enableMetricsCollection(bool enabled);
  bool isMetricsCollectionEnabled() const;
  void setMetricsUpdateInterval(std::chrono::milliseconds interval);
  JobMetrics getJobMetrics(const std::string &jobId) const;

private:
  std::shared_ptr<IETLJobRepository> jobRepo_;
  std::shared_ptr<DataTransformer> transformer_;
  std::shared_ptr<JobMonitorServiceInterface> monitorService_;

  std::queue<std::shared_ptr<ETLJob>> jobQueue_;
  mutable std::vector<std::shared_ptr<ETLJob>> jobs_;

  std::thread workerThread_;
  mutable std::timed_mutex jobMutex_;
  std::condition_variable_any jobCondition_;
  bool running_;

  // Metrics collection settings
  bool metricsCollectionEnabled_{true};
  std::chrono::milliseconds metricsUpdateInterval_{5000}; // 5 seconds default

  void workerLoop();
  void executeJob(std::shared_ptr<ETLJob> job);
  void executeJobWithMonitoring(std::shared_ptr<ETLJob> job);
  void executeExtractJob(std::shared_ptr<ETLJob> job);
  void executeTransformJob(std::shared_ptr<ETLJob> job);
  void executeLoadJob(std::shared_ptr<ETLJob> job);
  void executeFullETLJob(std::shared_ptr<ETLJob> job);

  // Helper methods for progress tracking
  void updateJobProgress(std::shared_ptr<ETLJob> job, int progress,
                         const std::string &step);
  void updateJobStatus(std::shared_ptr<ETLJob> job, JobStatus newStatus);

  // Metrics collection helpers
  void startJobMetricsCollection(std::shared_ptr<ETLJob> job);
  void stopJobMetricsCollection(std::shared_ptr<ETLJob> job);
  void updateJobMetricsFromCollector(std::shared_ptr<ETLJob> job);
  void setupMetricsCallback(std::shared_ptr<ETLJob> job);

  std::string generateJobId();
};
