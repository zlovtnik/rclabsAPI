#include "etl_job_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include "exceptions.hpp"
#include "exception_handler.hpp"
#include <iostream>
#include <random>
#include <sstream>

// Forward declaration for JobMonitorService to avoid circular dependency
class JobMonitorServiceInterface {
public:
    virtual ~JobMonitorServiceInterface() = default;
    virtual void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) = 0;
    virtual void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) = 0;
};

ETLJobManager::ETLJobManager(std::shared_ptr<DatabaseManager> dbManager,
                           std::shared_ptr<DataTransformer> transformer)
    : dbManager_(dbManager)
    , transformer_(transformer)
    , running_(false) {
}

ETLJobManager::~ETLJobManager() {
    stop();
}

std::string ETLJobManager::scheduleJob(const ETLJobConfig& config) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    auto job = std::make_shared<ETLJob>();
    job->jobId = config.jobId.empty() ? generateJobId() : config.jobId;
    job->type = config.type;
    job->status = JobStatus::PENDING;
    job->sourceConfig = config.sourceConfig;
    job->targetConfig = config.targetConfig;
    job->createdAt = std::chrono::system_clock::now();
    job->recordsProcessed = 0;
    job->recordsSuccessful = 0;
    job->recordsFailed = 0;
    
    jobs_.push_back(job);
    jobQueue_.push(job);
    
    jobCondition_.notify_one();
    
    ETL_LOG_INFO("Scheduled job: " + job->jobId + " (type: " + std::to_string(static_cast<int>(job->type)) + ")");
    return job->jobId;
}

bool ETLJobManager::cancelJob(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    for (auto& job : jobs_) {
        if (job->jobId == jobId && job->status == JobStatus::PENDING) {
            job->status = JobStatus::CANCELLED;
            std::cout << "Cancelled job: " << jobId << std::endl;
            return true;
        }
    }
    
    return false;
}

bool ETLJobManager::pauseJob(const std::string& jobId) {
    // For simplicity, not implementing pause/resume functionality
    std::cout << "Pause job not implemented: " << jobId << std::endl;
    return false;
}

bool ETLJobManager::resumeJob(const std::string& jobId) {
    // For simplicity, not implementing pause/resume functionality
    std::cout << "Resume job not implemented: " << jobId << std::endl;
    return false;
}

std::shared_ptr<ETLJob> ETLJobManager::getJob(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    for (const auto& job : jobs_) {
        if (job->jobId == jobId) {
            return job;
        }
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<ETLJob>> ETLJobManager::getAllJobs() {
    std::lock_guard<std::mutex> lock(jobMutex_);
    return jobs_;
}

std::vector<std::shared_ptr<ETLJob>> ETLJobManager::getJobsByStatus(JobStatus status) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    std::vector<std::shared_ptr<ETLJob>> result;
    for (const auto& job : jobs_) {
        if (job->status == status) {
            result.push_back(job);
        }
    }
    
    return result;
}

void ETLJobManager::start() {
    if (running_) {
        ETL_LOG_WARN("ETL Job Manager is already running");
        return;
    }
    
    ETL_LOG_INFO("Starting ETL Job Manager");
    running_ = true;
    workerThread_ = std::thread(&ETLJobManager::workerLoop, this);
    ETL_LOG_INFO("ETL Job Manager started successfully");
}

void ETLJobManager::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    jobCondition_.notify_all();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    std::cout << "ETL Job Manager stopped" << std::endl;
}

bool ETLJobManager::isRunning() const {
    return running_;
}

void ETLJobManager::setJobMonitorService(std::shared_ptr<JobMonitorServiceInterface> monitor) {
    monitorService_ = monitor;
    ETL_LOG_INFO("Job monitor service attached to ETL Job Manager");
}

void ETLJobManager::publishJobStatusUpdate(const std::string& jobId, JobStatus status) {
    if (monitorService_) {
        auto job = getJob(jobId);
        if (job) {
            JobStatus oldStatus = job->status;
            job->status = status; // Update the job status first
            monitorService_->onJobStatusChanged(jobId, oldStatus, status);
        }
    }
}

void ETLJobManager::publishJobProgress(const std::string& jobId, int progress, const std::string& step) {
    if (monitorService_) {
        monitorService_->onJobProgressUpdated(jobId, progress, step);
    }
}

void ETLJobManager::workerLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(jobMutex_);
        
        jobCondition_.wait(lock, [this] { return !jobQueue_.empty() || !running_; });
        
        if (!running_) {
            break;
        }
        
        if (!jobQueue_.empty()) {
            auto job = jobQueue_.front();
            jobQueue_.pop();
            lock.unlock();
            
            if (job->status == JobStatus::PENDING) {
                if (monitorService_) {
                    executeJobWithMonitoring(job);
                } else {
                    executeJob(job);
                }
            }
        }
    }
}

void ETLJobManager::executeJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Executing job: " << job->jobId << std::endl;
    
    job->status = JobStatus::RUNNING;
    job->startedAt = std::chrono::system_clock::now();
    
    ETLPlus::Exceptions::ErrorContext context("executeJob");
    context.addInfo("job_id", job->jobId);
    context.addInfo("job_type", std::to_string(static_cast<int>(job->type)));
    
    try {
        switch (job->type) {
            case JobType::EXTRACT:
                executeExtractJob(job);
                break;
            case JobType::TRANSFORM:
                executeTransformJob(job);
                break;
            case JobType::LOAD:
                executeLoadJob(job);
                break;
            case JobType::FULL_ETL:
                executeFullETLJob(job);
                break;
        }
        
        job->status = JobStatus::COMPLETED;
        ETL_LOG_INFO("Job completed successfully: " + job->jobId);
        
    } catch (const ETLPlus::Exceptions::BaseException& ex) {
        job->status = JobStatus::FAILED;
        job->errorMessage = ex.getMessage();
        ETL_LOG_ERROR("Job failed with ETL exception: " + job->jobId + " - " + ex.toLogString());
        
        // Re-throw to allow higher-level handling if needed
        throw;
        
    } catch (const std::exception& e) {
        job->status = JobStatus::FAILED;
        job->errorMessage = e.what();
        
        // Convert to ETL exception for consistent handling
        auto etlEx = ETLPlus::Exceptions::ETLException(
            ETLPlus::Exceptions::ErrorCode::JOB_EXECUTION_FAILED,
            "Job execution failed: " + std::string(e.what()),
            context,
            job->jobId);
        
        ETL_LOG_ERROR("Job failed with standard exception: " + job->jobId + " - " + etlEx.toLogString());
        throw etlEx;
        
    } catch (...) {
        job->status = JobStatus::FAILED;
        job->errorMessage = "Unknown error occurred during job execution";
        
        auto unknownEx = ETLPlus::Exceptions::ETLException(
            ETLPlus::Exceptions::ErrorCode::JOB_EXECUTION_FAILED,
            "Job execution failed with unknown error",
            context,
            job->jobId);
        
        ETL_LOG_ERROR("Job failed with unknown exception: " + job->jobId + " - " + unknownEx.toLogString());
        throw unknownEx;
    }
    
    job->completedAt = std::chrono::system_clock::now();
}

void ETLJobManager::executeExtractJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Extracting data from: " << job->sourceConfig << std::endl;
    
    // Simulate data extraction
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    job->recordsProcessed = 100;
    job->recordsSuccessful = 95;
    job->recordsFailed = 5;
}

void ETLJobManager::executeTransformJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Transforming data" << std::endl;
    
    // Create sample data
    std::vector<DataRecord> inputData;
    DataRecord record1;
    record1.fields["name"] = "John Doe";
    record1.fields["age"] = "30";
    inputData.push_back(record1);
    
    // Apply transformations
    auto transformedData = transformer_->transform(inputData);
    
    job->recordsProcessed = static_cast<int>(inputData.size());
    job->recordsSuccessful = static_cast<int>(transformedData.size());
    job->recordsFailed = 0;
}

void ETLJobManager::executeLoadJob(std::shared_ptr<ETLJob> job) {
    ETL_LOG_INFO("Starting load job for: " + job->targetConfig);
    
    ETLPlus::Exceptions::ErrorContext context("executeLoadJob");
    context.addInfo("job_id", job->jobId);
    context.addInfo("target_config", job->targetConfig);
    
    // Simulate data loading with proper transaction handling
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    if (!dbManager_->isConnected()) {
        throw ETLPlus::Exceptions::DatabaseException(
            ETLPlus::Exceptions::ErrorCode::CONNECTION_FAILED,
            "Database not connected for load operation",
            context);
    }

    // Use transaction scope for safe database operations
    WITH_DATABASE_TRANSACTION(dbManager_, "LoadJobData", {
        ETL_LOG_DEBUG("Executing load operation within transaction");
        
        // Simulate potential database operations that could fail
        if (!dbManager_->executeQuery("INSERT INTO processed_data VALUES (...)")) {
            throw ETLPlus::Exceptions::DatabaseException(
                ETLPlus::Exceptions::ErrorCode::QUERY_FAILED,
                "Failed to insert processed data",
                context,
                "INSERT INTO processed_data VALUES (...)");
        }
        
        // Additional validation - simulate constraint check
        if (job->jobId.find("fail") != std::string::npos) {
            throw ETLPlus::Exceptions::DatabaseException(
                ETLPlus::Exceptions::ErrorCode::CONSTRAINT_VIOLATION,
                "Simulated constraint violation during load",
                context);
        }
    });
    
    job->recordsProcessed = 95;
    job->recordsSuccessful = 90;
    job->recordsFailed = 5;
    
    ETL_LOG_INFO("Load job completed successfully");
}

void ETLJobManager::executeFullETLJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Executing full ETL pipeline" << std::endl;
    
    // Extract
    executeExtractJob(job);
    
    // Transform
    executeTransformJob(job);
    
    // Load
    executeLoadJob(job);
    
    std::cout << "Full ETL pipeline completed for job: " << job->jobId << std::endl;
}

void ETLJobManager::executeJobWithMonitoring(std::shared_ptr<ETLJob> job) {
    ETL_LOG_INFO("Executing job with monitoring: " + job->jobId);
    
    // Update status to RUNNING and publish event
    updateJobStatus(job, JobStatus::RUNNING);
    job->startedAt = std::chrono::system_clock::now();
    
    ETLPlus::Exceptions::ErrorContext context("executeJobWithMonitoring");
    context.addInfo("job_id", job->jobId);
    context.addInfo("job_type", std::to_string(static_cast<int>(job->type)));
    
    try {
        switch (job->type) {
            case JobType::EXTRACT:
                updateJobProgress(job, 0, "Starting data extraction");
                executeExtractJob(job);
                updateJobProgress(job, 100, "Data extraction completed");
                break;
            case JobType::TRANSFORM:
                updateJobProgress(job, 0, "Starting data transformation");
                executeTransformJob(job);
                updateJobProgress(job, 100, "Data transformation completed");
                break;
            case JobType::LOAD:
                updateJobProgress(job, 0, "Starting data loading");
                executeLoadJob(job);
                updateJobProgress(job, 100, "Data loading completed");
                break;
            case JobType::FULL_ETL:
                // Full ETL with detailed progress tracking
                updateJobProgress(job, 0, "Starting full ETL pipeline");
                
                updateJobProgress(job, 10, "Extracting data from source");
                executeExtractJob(job);
                
                updateJobProgress(job, 50, "Transforming extracted data");
                executeTransformJob(job);
                
                updateJobProgress(job, 80, "Loading transformed data");
                executeLoadJob(job);
                
                updateJobProgress(job, 100, "Full ETL pipeline completed");
                break;
        }
        
        updateJobStatus(job, JobStatus::COMPLETED);
        ETL_LOG_INFO("Job completed successfully with monitoring: " + job->jobId);
        
    } catch (const ETLPlus::Exceptions::BaseException& ex) {
        updateJobStatus(job, JobStatus::FAILED);
        job->errorMessage = ex.getMessage();
        ETL_LOG_ERROR("Job failed with ETL exception: " + job->jobId + " - " + ex.toLogString());
        throw;
        
    } catch (const std::exception& e) {
        updateJobStatus(job, JobStatus::FAILED);
        job->errorMessage = e.what();
        
        auto etlEx = ETLPlus::Exceptions::ETLException(
            ETLPlus::Exceptions::ErrorCode::JOB_EXECUTION_FAILED,
            "Job execution failed: " + std::string(e.what()),
            context,
            job->jobId);
        
        ETL_LOG_ERROR("Job failed with standard exception: " + job->jobId + " - " + etlEx.toLogString());
        throw etlEx;
        
    } catch (...) {
        updateJobStatus(job, JobStatus::FAILED);
        job->errorMessage = "Unknown error occurred during job execution";
        
        auto unknownEx = ETLPlus::Exceptions::ETLException(
            ETLPlus::Exceptions::ErrorCode::JOB_EXECUTION_FAILED,
            "Job execution failed with unknown error",
            context,
            job->jobId);
        
        ETL_LOG_ERROR("Job failed with unknown exception: " + job->jobId + " - " + unknownEx.toLogString());
        throw unknownEx;
    }
    
    job->completedAt = std::chrono::system_clock::now();
}

void ETLJobManager::updateJobProgress(std::shared_ptr<ETLJob> job, int progress, const std::string& step) {
    ETL_LOG_DEBUG("Job progress update: " + job->jobId + " - " + std::to_string(progress) + "% - " + step);
    
    if (monitorService_) {
        monitorService_->onJobProgressUpdated(job->jobId, progress, step);
    }
}

void ETLJobManager::updateJobStatus(std::shared_ptr<ETLJob> job, JobStatus newStatus) {
    JobStatus oldStatus = job->status;
    job->status = newStatus;
    
    ETL_LOG_INFO("Job status changed: " + job->jobId + " from " + 
                 std::to_string(static_cast<int>(oldStatus)) + " to " + 
                 std::to_string(static_cast<int>(newStatus)));
    
    if (monitorService_) {
        monitorService_->onJobStatusChanged(job->jobId, oldStatus, newStatus);
    }
}

std::string ETLJobManager::generateJobId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    return "job_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + 
           "_" + std::to_string(dis(gen));
}
