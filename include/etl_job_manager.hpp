#pragma once

#include "job_monitoring_models.hpp"
#include "lock_utils.hpp"
#include "system_metrics.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Forward declarations
class DataTransformer;
class DatabaseManager;
class ETLJobRepository;
class NotificationService;

struct ETLJobConfig {
  std::string jobId;
  JobType type;
  std::string sourceConfig;
  std::string targetConfig;
  std::string transformationRules;
  std::chrono::system_clock::time_point scheduledTime;
  bool isRecurring;
  std::chrono::minutes recurringInterval;
};

struct ETLJob {
  std::string jobId;
  JobType type;
  JobStatus status;
  std::string sourceConfig;
  std::string targetConfig;
  std::chrono::system_clock::time_point createdAt;
  std::chrono::system_clock::time_point startedAt;
  std::chrono::system_clock::time_point completedAt;
  std::string errorMessage;
  int recordsProcessed;
  int recordsSuccessful;
  int recordsFailed;

  // Enhanced metrics tracking
  JobMetrics metrics;
  std::shared_ptr<ETLPlus::Metrics::JobMetricsCollector> metricsCollector;
};

class DataTransformer;
class DatabaseManager;
class JobMonitorServiceInterface;

class ETLJobManager {
public:
  ETLJobManager(std::shared_ptr<DatabaseManager> dbManager,
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
  std::shared_ptr<DatabaseManager> dbManager_;
  std::shared_ptr<DataTransformer> transformer_;
  std::shared_ptr<ETLJobRepository> jobRepo_;
  std::shared_ptr<JobMonitorServiceInterface> monitorService_;

  std::queue<std::shared_ptr<ETLJob>> jobQueue_;
  std::vector<std::shared_ptr<ETLJob>> jobs_;

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
