#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

enum class JobStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

enum class JobType {
    EXTRACT,
    TRANSFORM,
    LOAD,
    FULL_ETL
};

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

class ETLJobManager {
public:
    ETLJobManager(std::shared_ptr<DatabaseManager> dbManager,
                  std::shared_ptr<DataTransformer> transformer);
    ~ETLJobManager();
    
    // Job management
    std::string scheduleJob(const ETLJobConfig& config);
    bool cancelJob(const std::string& jobId);
    bool pauseJob(const std::string& jobId);
    bool resumeJob(const std::string& jobId);
    
    // Job monitoring
    std::shared_ptr<ETLJob> getJob(const std::string& jobId);
    std::vector<std::shared_ptr<ETLJob>> getAllJobs();
    std::vector<std::shared_ptr<ETLJob>> getJobsByStatus(JobStatus status);
    
    // Job execution
    void start();
    void stop();
    bool isRunning() const;
    
private:
    std::shared_ptr<DatabaseManager> dbManager_;
    std::shared_ptr<DataTransformer> transformer_;
    
    std::queue<std::shared_ptr<ETLJob>> jobQueue_;
    std::vector<std::shared_ptr<ETLJob>> jobs_;
    
    std::thread workerThread_;
    std::mutex jobMutex_;
    std::condition_variable jobCondition_;
    bool running_;
    
    void workerLoop();
    void executeJob(std::shared_ptr<ETLJob> job);
    void executeExtractJob(std::shared_ptr<ETLJob> job);
    void executeTransformJob(std::shared_ptr<ETLJob> job);
    void executeLoadJob(std::shared_ptr<ETLJob> job);
    void executeFullETLJob(std::shared_ptr<ETLJob> job);
    
    std::string generateJobId();
};
