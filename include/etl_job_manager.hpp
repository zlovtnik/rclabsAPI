#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

enum class JobStatus { PENDING, RUNNING, COMPLETED, FAILED, CANCELLED };

enum class JobType { EXTRACT, TRANSFORM, LOAD, FULL_ETL };

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
};

class DataTransformer;
class DatabaseManager;
class JobMonitorService;

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
  std::shared_ptr<ETLJob> getJob(const std::string &jobId);
  std::vector<std::shared_ptr<ETLJob>> getAllJobs();
  std::vector<std::shared_ptr<ETLJob>> getJobsByStatus(JobStatus status);

  // Job execution
  void start();
  void stop();
  bool isRunning() const;

  // Job monitoring integration
  void setJobMonitorService(std::shared_ptr<JobMonitorService> monitor);
  void publishJobStatusUpdate(const std::string& jobId, JobStatus status);
  void publishJobProgress(const std::string& jobId, int progress, const std::string& step);

private:
  std::shared_ptr<DatabaseManager> dbManager_;
  std::shared_ptr<DataTransformer> transformer_;
  std::shared_ptr<JobMonitorService> monitorService_;

  std::queue<std::shared_ptr<ETLJob>> jobQueue_;
  std::vector<std::shared_ptr<ETLJob>> jobs_;

  std::thread workerThread_;
  std::mutex jobMutex_;
  std::condition_variable jobCondition_;
  bool running_;

  void workerLoop();
  void executeJob(std::shared_ptr<ETLJob> job);
  void executeJobWithMonitoring(std::shared_ptr<ETLJob> job);
  void executeExtractJob(std::shared_ptr<ETLJob> job);
  void executeTransformJob(std::shared_ptr<ETLJob> job);
  void executeLoadJob(std::shared_ptr<ETLJob> job);
  void executeFullETLJob(std::shared_ptr<ETLJob> job);
  
  // Helper methods for progress tracking
  void updateJobProgress(std::shared_ptr<ETLJob> job, int progress, const std::string& step);
  void updateJobStatus(std::shared_ptr<ETLJob> job, JobStatus newStatus);

  std::string generateJobId();
};
