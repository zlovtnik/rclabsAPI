#include "etl_job_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include "etl_exceptions.hpp"
#include "exception_handler.hpp"
#include "system_metrics.hpp"
#include <iostream>
#include <random>
#include <sstream>

// Forward declaration for JobMonitorService to avoid circular dependency
class JobMonitorServiceInterface {
public:
    virtual ~JobMonitorServiceInterface() = default;
    virtual void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) = 0;
    virtual void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) = 0;
    virtual void updateJobMetrics(const std::string& jobId, const JobMetrics& metrics) = 0;
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
    std::scoped_lock lock(jobMutex_);
    
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
    std::scoped_lock lock(jobMutex_);
    
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

std::shared_ptr<ETLJob> ETLJobManager::getJob(const std::string& jobId) const {
    std::scoped_lock lock(jobMutex_);
    
    for (const auto& job : jobs_) {
        if (job->jobId == jobId) {
            return job;
        }
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<ETLJob>> ETLJobManager::getAllJobs() const {
    std::scoped_lock lock(jobMutex_);
    return jobs_;
}

std::vector<std::shared_ptr<ETLJob>> ETLJobManager::getJobsByStatus(JobStatus status) const {
    std::scoped_lock lock(jobMutex_);
    
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

void ETLJobManager::publishJobMetrics(const std::string& jobId, const JobMetrics& metrics) {
    if (monitorService_) {
        monitorService_->updateJobMetrics(jobId, metrics);
    }
}

void ETLJobManager::enableMetricsCollection(bool enabled) {
    metricsCollectionEnabled_ = enabled;
    ETL_LOG_INFO("Metrics collection " + std::string(enabled ? "enabled" : "disabled"));
}

bool ETLJobManager::isMetricsCollectionEnabled() const {
    return metricsCollectionEnabled_;
}

void ETLJobManager::setMetricsUpdateInterval(std::chrono::milliseconds interval) {
    metricsUpdateInterval_ = interval;
    ETL_LOG_INFO("Metrics update interval set to " + std::to_string(interval.count()) + "ms");
}

JobMetrics ETLJobManager::getJobMetrics(const std::string& jobId) const {
    std::scoped_lock lock(jobMutex_);
    
    for (const auto& job : jobs_) {
        if (job->jobId == jobId) {
            if (job->metricsCollector && job->metricsCollector->isCollecting()) {
                // Return real-time metrics from collector
                auto snapshot = job->metricsCollector->getMetricsSnapshot();
                JobMetrics metrics = job->metrics;
                
                // Update with real-time data
                metrics.recordsProcessed = snapshot.recordsProcessed;
                metrics.recordsSuccessful = snapshot.recordsSuccessful;
                metrics.recordsFailed = snapshot.recordsFailed;
                metrics.processingRate = snapshot.processingRate;
                metrics.executionTime = snapshot.executionTime;
                metrics.memoryUsage = snapshot.memoryUsage;
                metrics.cpuUsage = snapshot.cpuUsage;
                
                return metrics;
            } else {
                // Return stored metrics
                return job->metrics;
            }
        }
    }
    
    return JobMetrics{}; // Return empty metrics if job not found
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
    
    etl::ErrorContext context;
    context["job_id"] = job->jobId;
    context["job_type"] = std::to_string(static_cast<int>(job->type));
    context["operation"] = "executeJob";
    
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
        
    } catch (const etl::ETLException& ex) {
        job->status = JobStatus::FAILED;
        job->errorMessage = ex.getMessage();
        ETL_LOG_ERROR("Job failed with ETL exception: " + job->jobId + " - " + ex.toLogString());
        
        // Re-throw to allow higher-level handling if needed
        throw;
        
    } catch (const std::exception& e) {
        job->status = JobStatus::FAILED;
        job->errorMessage = e.what();
        
        // Convert to ETL exception for consistent handling
        auto etlEx = etl::BusinessException(
            etl::ErrorCode::PROCESSING_FAILED,
            "Job execution failed: " + std::string(e.what()),
            "executeJob", context);
        
        ETL_LOG_ERROR("Job failed with standard exception: " + job->jobId + " - " + etlEx.toLogString());
        throw etlEx;
        
    } catch (...) {
        job->status = JobStatus::FAILED;
        job->errorMessage = "Unknown error occurred during job execution";
        
        auto unknownEx = etl::BusinessException(
            etl::ErrorCode::PROCESSING_FAILED,
            "Job execution failed with unknown error",
            "executeJob", context);
        
        ETL_LOG_ERROR("Job failed with unknown exception: " + job->jobId + " - " + unknownEx.toLogString());
        throw unknownEx;
    }
    
    job->completedAt = std::chrono::system_clock::now();
}

void ETLJobManager::executeExtractJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Extracting data from: " << job->sourceConfig << std::endl;
    
    // Simulate data extraction with metrics collection
    const int totalRecords = 100;
    const int batchSize = 20;
    const size_t bytesPerRecord = 512; // Approximate size per record
    
    for (int processed = 0; processed < totalRecords; processed += batchSize) {
        int currentBatch = std::min(batchSize, totalRecords - processed);
        
        // Simulate processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Simulate success/failure rates (95% success rate)
        int successful = static_cast<int>(currentBatch * 0.95);
        int failed = currentBatch - successful;
        
        // Record metrics if collector is available
        if (job->metricsCollector && job->metricsCollector->isCollecting()) {
            job->metricsCollector->recordBatchProcessed(currentBatch, successful, failed);
            
            // Record batch processing for advanced metrics
            size_t batchBytes = currentBatch * bytesPerRecord;
            job->metrics.recordBatch(currentBatch, successful, failed, batchBytes);
        }
        
        // Update job statistics
        job->recordsProcessed += currentBatch;
        job->recordsSuccessful += successful;
        job->recordsFailed += failed;
    }
    
    // Final batch size
    job->metrics.totalBytesProcessed += totalRecords * bytesPerRecord;
}

void ETLJobManager::executeTransformJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Transforming data" << std::endl;
    
    // Create sample data
    std::vector<DataRecord> inputData;
    DataRecord record1;
    record1.fields["name"] = "John Doe";
    record1.fields["age"] = "30";
    inputData.push_back(record1);
    
    const int totalRecords = static_cast<int>(inputData.size());
    const size_t bytesPerRecord = 256; // Transformed records are smaller
    
    // Apply transformations with metrics tracking
    auto transformedData = transformer_->transform(inputData);
    
    // Simulate transformation processing with batches
    int successful = static_cast<int>(transformedData.size());
    int failed = totalRecords - successful;
    
    // Record metrics if collector is available
    if (job->metricsCollector && job->metricsCollector->isCollecting()) {
        job->metricsCollector->recordBatchProcessed(totalRecords, successful, failed);
        
        // Record transformation metrics
        size_t inputBytes = totalRecords * 512; // Input size
        size_t outputBytes = successful * bytesPerRecord; // Output size
        job->metrics.recordBatch(totalRecords, successful, failed, inputBytes);
        job->metrics.totalBytesWritten += outputBytes;
    }
    
    job->recordsProcessed += totalRecords;
    job->recordsSuccessful += successful;
    job->recordsFailed += failed;
}

void ETLJobManager::executeLoadJob(std::shared_ptr<ETLJob> job) {
    ETL_LOG_INFO("Starting load job for: " + job->targetConfig);
    
    etl::ErrorContext context;
    context["job_id"] = job->jobId;
    context["target_config"] = job->targetConfig;
    context["operation"] = "executeLoadJob";
    
    if (!dbManager_->isConnected()) {
        throw etl::SystemException(
            etl::ErrorCode::DATABASE_ERROR,
            "Database not connected for load operation",
            "ETLJobManager", context);
    }

    // Simulate data loading with metrics collection
    const int totalRecords = 95;
    const int batchSize = 10;
    const size_t bytesPerRecord = 128; // Database records are more compact
    
    // Use transaction scope for safe database operations
    try {
        ETLPlus::ExceptionHandling::TransactionScope transaction(dbManager_, "LoadJobData");
        
        ETL_LOG_DEBUG("Executing load operation within transaction");
        
        for (int processed = 0; processed < totalRecords; processed += batchSize) {
            int currentBatch = std::min(batchSize, totalRecords - processed);
            
            // Simulate database operation time
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            // Simulate potential database operations that could fail
            if (!dbManager_->executeQuery("INSERT INTO processed_data VALUES (...)")) {
                throw etl::SystemException(
                    etl::ErrorCode::DATABASE_ERROR,
                    "Failed to insert processed data",
                    "ETLJobManager", context);
            }
            
            // Simulate success/failure rates (94% success rate for database operations)
            int successful = static_cast<int>(currentBatch * 0.94);
            int failed = currentBatch - successful;
            
            // Additional validation - simulate constraint check
            if (job->jobId.find("fail") != std::string::npos) {
                throw etl::SystemException(
                    etl::ErrorCode::CONSTRAINT_VIOLATION,
                    "Simulated constraint violation during load",
                    "ETLJobManager", context);
            }
            
            // Record metrics if collector is available
            if (job->metricsCollector && job->metricsCollector->isCollecting()) {
                job->metricsCollector->recordBatchProcessed(currentBatch, successful, failed);
                
                // Record load metrics
                size_t batchBytes = currentBatch * bytesPerRecord;
                job->metrics.recordBatch(currentBatch, successful, failed, batchBytes);
                job->metrics.totalBytesWritten += successful * bytesPerRecord;
            }
            
            // Update job statistics
            job->recordsProcessed += currentBatch;
            job->recordsSuccessful += successful;
            job->recordsFailed += failed;
        }
        
        transaction.commit();
    } catch (...) {
        // Transaction will automatically rollback in destructor
        throw;
    }
    
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
    
    // Start metrics collection if enabled
    if (metricsCollectionEnabled_) {
        startJobMetricsCollection(job);
    }
    
    // Update status to RUNNING and publish event
    updateJobStatus(job, JobStatus::RUNNING);
    job->startedAt = std::chrono::system_clock::now();
    job->metrics.startTime = job->startedAt;
    
    etl::ErrorContext context;
    context["job_id"] = job->jobId;
    context["job_type"] = std::to_string(static_cast<int>(job->type));
    context["operation"] = "executeJobWithMonitoring";
    
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
                // Full ETL with detailed progress tracking and metrics
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
        
    } catch (const etl::ETLException& ex) {
        updateJobStatus(job, JobStatus::FAILED);
        job->errorMessage = ex.getMessage();
        
        // Record error in metrics
        if (job->metricsCollector && job->metricsCollector->isCollecting()) {
            job->metricsCollector->recordFailedRecord();
            job->metrics.recordError();
        }
        
        ETL_LOG_ERROR("Job failed with ETL exception: " + job->jobId + " - " + ex.toLogString());
        throw;
        
    } catch (const std::exception& e) {
        updateJobStatus(job, JobStatus::FAILED);
        job->errorMessage = e.what();
        
        // Record error in metrics
        if (job->metricsCollector && job->metricsCollector->isCollecting()) {
            job->metricsCollector->recordFailedRecord();
            job->metrics.recordError();
        }
        
        auto etlEx = etl::BusinessException(
            etl::ErrorCode::PROCESSING_FAILED,
            "Job execution failed: " + std::string(e.what()),
            "executeJobWithMonitoring", context);
        
        ETL_LOG_ERROR("Job failed with standard exception: " + job->jobId + " - " + etlEx.toLogString());
        throw etlEx;
        
    } catch (...) {
        updateJobStatus(job, JobStatus::FAILED);
        job->errorMessage = "Unknown error occurred during job execution";
        
        // Record error in metrics
        if (job->metricsCollector && job->metricsCollector->isCollecting()) {
            job->metricsCollector->recordFailedRecord();
            job->metrics.recordError();
        }
        
        auto unknownEx = etl::BusinessException(
            etl::ErrorCode::PROCESSING_FAILED,
            "Job execution failed with unknown error",
            "executeJobWithMonitoring", context);
        
        ETL_LOG_ERROR("Job failed with unknown exception: " + job->jobId + " - " + unknownEx.toLogString());
        throw unknownEx;
    }
    
    job->completedAt = std::chrono::system_clock::now();
    
    // Stop metrics collection and finalize metrics
    if (metricsCollectionEnabled_) {
        stopJobMetricsCollection(job);
    }
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

void ETLJobManager::startJobMetricsCollection(std::shared_ptr<ETLJob> job) {
    if (!metricsCollectionEnabled_) {
        return;
    }
    
    // Create metrics collector for this job
    job->metricsCollector = std::make_shared<ETLPlus::Metrics::JobMetricsCollector>(job->jobId);
    
    // Set up real-time metrics callback
    setupMetricsCallback(job);
    
    // Start collection
    job->metricsCollector->setUpdateInterval(metricsUpdateInterval_);
    job->metricsCollector->startCollection();
    
    ETL_LOG_INFO("Started metrics collection for job: " + job->jobId);
}

void ETLJobManager::stopJobMetricsCollection(std::shared_ptr<ETLJob> job) {
    if (!job->metricsCollector) {
        return;
    }
    
    // Stop collection
    job->metricsCollector->stopCollection();
    
    // Update final metrics from collector
    updateJobMetricsFromCollector(job);
    
    // Finalize metrics calculations
    auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        job->completedAt - job->startedAt);
    job->metrics.executionTime = executionTime;
    job->metrics.updatePerformanceIndicators();
    job->metrics.calculateAverages();
    
    // Publish final metrics
    if (monitorService_) {
        publishJobMetrics(job->jobId, job->metrics);
    }
    
    ETL_LOG_INFO("Stopped metrics collection for job: " + job->jobId + " - " + 
                 job->metrics.getPerformanceSummary());
}

void ETLJobManager::updateJobMetricsFromCollector(std::shared_ptr<ETLJob> job) {
    if (!job->metricsCollector) {
        return;
    }
    
    auto snapshot = job->metricsCollector->getMetricsSnapshot();
    
    // Update job metrics from collector snapshot
    job->metrics.recordsProcessed = snapshot.recordsProcessed;
    job->metrics.recordsSuccessful = snapshot.recordsSuccessful;
    job->metrics.recordsFailed = snapshot.recordsFailed;
    job->metrics.processingRate = snapshot.processingRate;
    job->metrics.executionTime = snapshot.executionTime;
    job->metrics.memoryUsage = snapshot.memoryUsage;
    job->metrics.cpuUsage = snapshot.cpuUsage;
    
    // Update the basic job fields for compatibility
    job->recordsProcessed = snapshot.recordsProcessed;
    job->recordsSuccessful = snapshot.recordsSuccessful;
    job->recordsFailed = snapshot.recordsFailed;
}

void ETLJobManager::setupMetricsCallback(std::shared_ptr<ETLJob> job) {
    if (!job->metricsCollector || !monitorService_) {
        return;
    }
    
    // Set up callback for real-time metrics updates
    auto metricsCallback = [this](
        const std::string& callbackJobId,
        const ETLPlus::Metrics::JobMetricsCollector::MetricsSnapshot& snapshot) {
        JobMetrics metricsCopy;
        {
            // Find the job and update its metrics
            std::scoped_lock lock(jobMutex_);
            for (auto& jobPtr : jobs_) {
                if (jobPtr->jobId == callbackJobId) {
                    // Update metrics from snapshot
                    JobMetrics metrics = jobPtr->metrics;
                    metrics.recordsProcessed = snapshot.recordsProcessed;
                    metrics.recordsSuccessful = snapshot.recordsSuccessful;
                    metrics.recordsFailed = snapshot.recordsFailed;
                    metrics.processingRate = snapshot.processingRate;
                    metrics.executionTime = snapshot.executionTime;
                    metrics.memoryUsage = snapshot.memoryUsage;
                    metrics.cpuUsage = snapshot.cpuUsage;
                    metrics.lastUpdateTime = snapshot.timestamp;
                    metrics.updatePerformanceIndicators();
                    jobPtr->metrics = metrics;
                    metricsCopy = metrics; // copy for publishing outside lock
                    break;
                }
            }
        }
        // Publish outside of lock to avoid deadlocks/re-entrancy
        if (metricsCopy.lastUpdateTime.time_since_epoch().count() != 0) {
            publishJobMetrics(callbackJobId, metricsCopy);
        }
    };
    
    job->metricsCollector->setMetricsUpdateCallback(metricsCallback);
}
