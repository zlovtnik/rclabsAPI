#include "job_monitor_service.hpp"
#include "websocket_manager.hpp"
#include "notification_service.hpp"
#include "logger.hpp"
#include <algorithm>
#include <chrono>

JobMonitorService::JobMonitorService() 
    : circuitBreaker_(5, std::chrono::seconds(60), 3)
    , pendingStatusUpdates_(10000)
    , pendingProgressUpdates_(10000) {
    
    // Initialize recovery configuration
    recoveryConfig_.enableGracefulDegradation = true;
    recoveryConfig_.enableAutoRecovery = true;
    recoveryConfig_.maxRecoveryAttempts = 3;
    recoveryConfig_.baseRecoveryDelay = std::chrono::milliseconds(5000);
    recoveryConfig_.maxRecoveryDelay = std::chrono::milliseconds(60000);
    recoveryConfig_.backoffMultiplier = 2.0;
    recoveryConfig_.eventQueueMaxSize = 10000;
    recoveryConfig_.healthCheckInterval = std::chrono::seconds(30);
    recoveryConfig_.enableHealthChecks = true;
    recoveryConfig_.maxFailedHealthChecks = 3;
    
    JOB_LOG_DEBUG("Job Monitor Service created with error handling and recovery capabilities");
}

JobMonitorService::~JobMonitorService() {
    stop();
    JOB_LOG_DEBUG("Job Monitor Service destroyed");
}

void JobMonitorService::initialize(std::shared_ptr<ETLJobManager> etlManager,
                                 std::shared_ptr<WebSocketManager> wsManager,
                                 std::shared_ptr<NotificationService> notifier) {
    if (!etlManager) {
        throw std::invalid_argument("ETL Job Manager cannot be null");
    }
    if (!wsManager) {
        throw std::invalid_argument("WebSocket Manager cannot be null");
    }

    etlManager_ = etlManager;
    wsManager_ = wsManager;
    notifier_ = notifier;

    // Set this service as the monitor for the ETL Job Manager
    etlManager_->setJobMonitorService(std::shared_ptr<JobMonitorServiceInterface>(this, [](JobMonitorServiceInterface*){
        // Custom deleter that does nothing - the JobMonitorService will be managed elsewhere
    }));

    JOB_LOG_INFO("Job Monitor Service initialized with ETL Job Manager and WebSocket Manager");
    if (notifier_) {
        JOB_LOG_INFO("Notification service attached to Job Monitor Service");
    }
}

void JobMonitorService::start() {
    if (running_) {
        JOB_LOG_WARN("Job Monitor Service is already running");
        return;
    }

    running_ = true;
    
    // Initialize monitoring data for any existing jobs
    if (etlManager_) {
        auto existingJobs = etlManager_->getAllJobs();
        std::scoped_lock lock(jobDataMutex_);
        
        for (const auto& job : existingJobs) {
            JobMonitoringData monitoringData;
            monitoringData.jobId = job->jobId;
            monitoringData.jobType = job->type;
            monitoringData.status = job->status;
            monitoringData.startTime = job->startedAt;
            monitoringData.createdAt = job->createdAt;
            monitoringData.completedAt = job->completedAt;
            monitoringData.currentStep = "Initialized from existing job";
            
            // Set metrics from job data
            monitoringData.metrics.recordsProcessed = job->recordsProcessed;
            monitoringData.metrics.recordsSuccessful = job->recordsSuccessful;
            monitoringData.metrics.recordsFailed = job->recordsFailed;
            
            if (!job->errorMessage.empty()) {
                monitoringData.errorMessage = job->errorMessage;
            }
            
            if (monitoringData.isActive()) {
                activeJobs_[job->jobId] = monitoringData;
            } else {
                completedJobs_[job->jobId] = monitoringData;
            }
        }
        
        JOB_LOG_INFO("Initialized monitoring data for " + std::to_string(existingJobs.size()) + " existing jobs");
    }

    JOB_LOG_INFO("Job Monitor Service started");
}

void JobMonitorService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Clear monitoring data
    std::scoped_lock lock(jobDataMutex_);
    activeJobs_.clear();
    completedJobs_.clear();

    JOB_LOG_INFO("Job Monitor Service stopped");
}

bool JobMonitorService::isRunning() const {
    return running_;
}

void JobMonitorService::onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) {
    if (!running_) {
        JOB_LOG_WARN("Job Monitor Service not running, ignoring status change for job: " + jobId);
        return;
    }

    JOB_LOG_INFO("Job status changed: " + jobId + " from " + 
                 jobStatusToString(oldStatus) + " to " + jobStatusToString(newStatus));

    // Create or update job monitoring data
    if (oldStatus == JobStatus::PENDING && newStatus == JobStatus::RUNNING) {
        createJobMonitoringData(jobId);
    }

    // Update job status and handle state transitions
    updateJobMonitoringData(jobId, [&](JobMonitoringData& data) {
        data.status = newStatus;
        
        if (newStatus == JobStatus::RUNNING && data.startTime == std::chrono::system_clock::time_point{}) {
            data.startTime = std::chrono::system_clock::now();
        }
        
        if (newStatus == JobStatus::COMPLETED || newStatus == JobStatus::FAILED || newStatus == JobStatus::CANCELLED) {
            data.completedAt = std::chrono::system_clock::now();
            data.updateExecutionTime();
        }
    });

    // Create and broadcast status update
    auto statusUpdate = createJobStatusUpdate(jobId, oldStatus, newStatus);
    broadcastJobStatusUpdate(statusUpdate);

    // Handle job completion - move to completed jobs
    if (newStatus == JobStatus::COMPLETED || newStatus == JobStatus::FAILED || newStatus == JobStatus::CANCELLED) {
        moveJobToCompleted(jobId);
    }

    // Check for notifications
    checkAndSendNotifications(jobId, oldStatus, newStatus);
}

void JobMonitorService::onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) {
    if (!running_) {
        JOB_LOG_WARN("Job Monitor Service not running, ignoring progress update for job: " + jobId);
        return;
    }

    // Check if we should update progress (avoid too frequent updates)
    if (!shouldUpdateProgress(jobId, progressPercent)) {
        return;
    }

    JOB_LOG_DEBUG("Job progress updated: " + jobId + " - " + std::to_string(progressPercent) + "% - " + currentStep);

    // Update job monitoring data
    updateJobMonitoringData(jobId, [&](JobMonitoringData& data) {
        data.progressPercent = progressPercent;
        data.currentStep = currentStep;
        data.updateExecutionTime();
    });

    // Broadcast progress update
    broadcastJobProgress(jobId, progressPercent, currentStep);
}

void JobMonitorService::onJobLogGenerated(const std::string& jobId, const LogMessage& logMessage) {
    if (!running_) {
        return;
    }

    JOB_LOG_DEBUG("Log message generated for job: " + jobId + " - " + logMessage.level + ": " + logMessage.message);

    // Add log to job's recent logs
    addLogToJob(jobId, logMessage.message);

    // Broadcast log message to WebSocket clients
    broadcastLogMessage(logMessage);
}

JobMonitoringData JobMonitorService::getJobMonitoringData(const std::string& jobId) {
    return withJobDataLock<JobMonitoringData>([&]() -> JobMonitoringData {
        // Check active jobs first
        auto activeIt = activeJobs_.find(jobId);
        if (activeIt != activeJobs_.end()) {
            return activeIt->second;
        }

        // Check completed jobs
        auto completedIt = completedJobs_.find(jobId);
        if (completedIt != completedJobs_.end()) {
            return completedIt->second;
        }

        // Job not found, try to create from ETL Job Manager
        if (etlManager_) {
            auto job = etlManager_->getJob(jobId);
            if (job) {
                JobMonitoringData data;
                data.jobId = job->jobId;
                data.jobType = job->type;
                data.status = job->status;
                data.startTime = job->startedAt;
                data.createdAt = job->createdAt;
                data.completedAt = job->completedAt;
                data.currentStep = "Retrieved from ETL Job Manager";
                
                data.metrics.recordsProcessed = job->recordsProcessed;
                data.metrics.recordsSuccessful = job->recordsSuccessful;
                data.metrics.recordsFailed = job->recordsFailed;
                
                if (!job->errorMessage.empty()) {
                    data.errorMessage = job->errorMessage;
                }
                
                return data;
            }
        }

        // Return empty data if job not found
        JobMonitoringData emptyData;
        emptyData.jobId = jobId;
        return emptyData;
    });
}

std::vector<JobMonitoringData> JobMonitorService::getAllActiveJobs() {
    return withJobDataLock<std::vector<JobMonitoringData>>([&]() -> std::vector<JobMonitoringData> {
        std::vector<JobMonitoringData> result;
        result.reserve(activeJobs_.size());
        
        for (const auto& [jobId, data] : activeJobs_) {
            result.push_back(data);
        }
        
        return result;
    });
}

std::vector<JobMonitoringData> JobMonitorService::getJobsByStatus(JobStatus status) {
    return withJobDataLock<std::vector<JobMonitoringData>>([&]() -> std::vector<JobMonitoringData> {
        std::vector<JobMonitoringData> result;
        
        // Search in active jobs
        for (const auto& [jobId, data] : activeJobs_) {
            if (data.status == status) {
                result.push_back(data);
            }
        }
        
        // Search in completed jobs if not an active status
        if (status == JobStatus::COMPLETED || status == JobStatus::FAILED || status == JobStatus::CANCELLED) {
            for (const auto& [jobId, data] : completedJobs_) {
                if (data.status == status) {
                    result.push_back(data);
                }
            }
        }
        
        return result;
    });
}

std::vector<JobMonitoringData> JobMonitorService::getJobsByType(JobType type) {
    return withJobDataLock<std::vector<JobMonitoringData>>([&]() -> std::vector<JobMonitoringData> {
        std::vector<JobMonitoringData> result;
        
        // Search in active jobs
        for (const auto& [jobId, data] : activeJobs_) {
            if (data.jobType == type) {
                result.push_back(data);
            }
        }
        
        // Search in completed jobs
        for (const auto& [jobId, data] : completedJobs_) {
            if (data.jobType == type) {
                result.push_back(data);
            }
        }
        
        return result;
    });
}

void JobMonitorService::broadcastJobStatusUpdate(const JobStatusUpdate& update) {
    if (!wsManager_) {
        JOB_LOG_WARN("WebSocket Manager not available, cannot broadcast job status update");
        return;
    }

    auto message = WebSocketMessage::createJobStatusUpdate(update);
    wsManager_->broadcastJobUpdate(message.toJson(), update.jobId);
    
    JOB_LOG_DEBUG("Broadcasted job status update for job: " + update.jobId);
}

void JobMonitorService::broadcastJobProgress(const std::string& jobId, int progressPercent, const std::string& currentStep) {
    if (!wsManager_) {
        JOB_LOG_WARN("WebSocket Manager not available, cannot broadcast job progress");
        return;
    }

    auto message = createProgressMessage(jobId, progressPercent, currentStep);
    wsManager_->broadcastByMessageType(message.toJson(), MessageType::JOB_PROGRESS_UPDATE, jobId);
    
    JOB_LOG_DEBUG("Broadcasted job progress for job: " + jobId + " - " + std::to_string(progressPercent) + "%");
}

void JobMonitorService::broadcastLogMessage(const LogMessage& logMessage) {
    if (!wsManager_) {
        JOB_LOG_WARN("WebSocket Manager not available, cannot broadcast log message");
        return;
    }

    auto message = WebSocketMessage::createLogMessage(logMessage);
    wsManager_->broadcastLogMessage(message.toJson(), logMessage.jobId, logMessage.level);
    
    JOB_LOG_DEBUG("Broadcasted log message for job: " + logMessage.jobId);
}

void JobMonitorService::broadcastJobMetrics(const std::string& jobId, const JobMetrics& metrics) {
    if (!wsManager_) {
        JOB_LOG_WARN("WebSocket Manager not available, cannot broadcast job metrics");
        return;
    }

    auto message = WebSocketMessage::createMetricsUpdate(jobId, metrics);
    wsManager_->broadcastByMessageType(message.toJson(), MessageType::JOB_METRICS_UPDATE, jobId);
    
    JOB_LOG_DEBUG("Broadcasted job metrics for job: " + jobId);
}

size_t JobMonitorService::getActiveJobCount() const {
    std::scoped_lock lock(jobDataMutex_);
    return activeJobs_.size();
}

std::vector<std::string> JobMonitorService::getActiveJobIds() const {
    return withJobDataLock<std::vector<std::string>>([&]() -> std::vector<std::string> {
        std::vector<std::string> result;
        result.reserve(activeJobs_.size());
        
        for (const auto& [jobId, data] : activeJobs_) {
            result.push_back(jobId);
        }
        
        return result;
    });
}

bool JobMonitorService::isJobActive(const std::string& jobId) const {
    std::scoped_lock lock(jobDataMutex_);
    return activeJobs_.find(jobId) != activeJobs_.end();
}

JobMetrics JobMonitorService::getJobMetrics(const std::string& jobId) const {
    return withJobDataLock<JobMetrics>([&]() -> JobMetrics {
        auto activeIt = activeJobs_.find(jobId);
        if (activeIt != activeJobs_.end()) {
            return activeIt->second.metrics;
        }

        auto completedIt = completedJobs_.find(jobId);
        if (completedIt != completedJobs_.end()) {
            return completedIt->second.metrics;
        }

        return JobMetrics{}; // Return empty metrics if job not found
    });
}

void JobMonitorService::updateJobMetrics(const std::string& jobId, const JobMetrics& metrics) {
    updateJobMonitoringData(jobId, [&](JobMonitoringData& data) {
        data.metrics = metrics;
        data.updateExecutionTime();
    });

    // Store metrics snapshot for historical analysis
    storeMetricsSnapshot(jobId, metrics);

    // Broadcast metrics update
    broadcastJobMetrics(jobId, metrics);
    
    JOB_LOG_DEBUG("Updated job metrics for job: " + jobId);
}

std::vector<JobMetrics> JobMonitorService::getJobMetricsHistory(const std::string& jobId, 
                                                               std::chrono::system_clock::time_point since) const {
    std::scoped_lock lock(metricsHistoryMutex_);
    
    auto historyIt = metricsHistory_.find(jobId);
    if (historyIt == metricsHistory_.end()) {
        return {}; // No history found
    }
    
    std::vector<JobMetrics> result;
    for (const auto& metrics : historyIt->second) {
        if (since == std::chrono::system_clock::time_point{} || metrics.lastUpdateTime >= since) {
            result.push_back(metrics);
        }
    }
    
    return result;
}

JobMetrics JobMonitorService::getAggregatedMetrics(const std::vector<std::string>& jobIds) const {
    std::vector<JobMetrics> metricsCollection;
    
    for (const auto& jobId : jobIds) {
        auto metrics = getJobMetrics(jobId);
        if (metrics.recordsProcessed > 0) { // Only include jobs with actual data
            metricsCollection.push_back(metrics);
        }
    }
    
    return aggregateMetrics(metricsCollection);
}

JobMetrics JobMonitorService::getAggregatedMetricsByType(JobType jobType) const {
    std::vector<JobMetrics> metricsCollection;
    
    withJobDataLock<void>([&]() {
        // Collect from active jobs
        for (const auto& [jobId, data] : activeJobs_) {
            if (data.jobType == jobType && data.metrics.recordsProcessed > 0) {
                metricsCollection.push_back(data.metrics);
            }
        }
        
        // Collect from completed jobs
        for (const auto& [jobId, data] : completedJobs_) {
            if (data.jobType == jobType && data.metrics.recordsProcessed > 0) {
                metricsCollection.push_back(data.metrics);
            }
        }
    });
    
    return aggregateMetrics(metricsCollection);
}

JobMetrics JobMonitorService::getAggregatedMetricsByTimeRange(std::chrono::system_clock::time_point start,
                                                             std::chrono::system_clock::time_point end) const {
    std::vector<JobMetrics> metricsCollection;
    
    withJobDataLock<void>([&]() {
        // Collect from active jobs
        for (const auto& [jobId, data] : activeJobs_) {
            if (data.startTime >= start && data.startTime <= end && data.metrics.recordsProcessed > 0) {
                metricsCollection.push_back(data.metrics);
            }
        }
        
        // Collect from completed jobs
        for (const auto& [jobId, data] : completedJobs_) {
            if (data.startTime >= start && data.startTime <= end && data.metrics.recordsProcessed > 0) {
                metricsCollection.push_back(data.metrics);
            }
        }
    });
    
    return aggregateMetrics(metricsCollection);
}

double JobMonitorService::getAverageProcessingRate(std::optional<JobType> jobType) const {
    std::vector<double> rates;
    
    withJobDataLock<void>([&]() {
        // Collect from active jobs
        for (const auto& [jobId, data] : activeJobs_) {
            if ((!jobType.has_value() || data.jobType == jobType.value()) &&
                data.metrics.averageProcessingRate > 0) {
                rates.push_back(data.metrics.averageProcessingRate);
            }
        }
        
        // Collect from completed jobs
        for (const auto& [jobId, data] : completedJobs_) {
            if ((!jobType.has_value() || data.jobType == jobType.value()) &&
                data.metrics.averageProcessingRate > 0) {
                rates.push_back(data.metrics.averageProcessingRate);
            }
        }
    });
    
    if (rates.empty()) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (double rate : rates) {
        sum += rate;
    }
    
    return sum / rates.size();
}

double JobMonitorService::getAverageErrorRate(std::optional<JobType> jobType) const {
    std::vector<double> errorRates;
    
    withJobDataLock<void>([&]() {
        // Collect from active jobs
        for (const auto& [jobId, data] : activeJobs_) {
            if ((!jobType.has_value() || data.jobType == jobType.value()) &&
                data.metrics.recordsProcessed > 0) {
                errorRates.push_back(data.metrics.errorRate);
            }
        }
        
        // Collect from completed jobs
        for (const auto& [jobId, data] : completedJobs_) {
            if ((!jobType.has_value() || data.jobType == jobType.value()) &&
                data.metrics.recordsProcessed > 0) {
                errorRates.push_back(data.metrics.errorRate);
            }
        }
    });
    
    if (errorRates.empty()) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (double rate : errorRates) {
        sum += rate;
    }
    
    return sum / errorRates.size();
}

std::pair<JobMetrics, JobMetrics> JobMonitorService::getPerformanceBenchmarks() const {
    std::vector<JobMetrics> allMetrics;
    
    withJobDataLock<void>([&]() {
        // Collect all metrics for benchmark calculation
        for (const auto& [jobId, data] : activeJobs_) {
            if (data.metrics.recordsProcessed > 0) {
                allMetrics.push_back(data.metrics);
            }
        }
        
        for (const auto& [jobId, data] : completedJobs_) {
            if (data.metrics.recordsProcessed > 0) {
                allMetrics.push_back(data.metrics);
            }
        }
    });
    
    if (allMetrics.empty()) {
        return {JobMetrics{}, JobMetrics{}};
    }
    
    // Calculate min and max benchmarks
    JobMetrics minBenchmark = allMetrics[0];
    JobMetrics maxBenchmark = allMetrics[0];
    
    for (const auto& metrics : allMetrics) {
        // Update minimum benchmarks (worst performance)
        if (metrics.averageProcessingRate < minBenchmark.averageProcessingRate && metrics.averageProcessingRate > 0) {
            minBenchmark.averageProcessingRate = metrics.averageProcessingRate;
        }
        if (metrics.errorRate > minBenchmark.errorRate) {
            minBenchmark.errorRate = metrics.errorRate;
        }
        if (metrics.memoryEfficiency < minBenchmark.memoryEfficiency && metrics.memoryEfficiency > 0) {
            minBenchmark.memoryEfficiency = metrics.memoryEfficiency;
        }
        
        // Update maximum benchmarks (best performance)
        if (metrics.averageProcessingRate > maxBenchmark.averageProcessingRate) {
            maxBenchmark.averageProcessingRate = metrics.averageProcessingRate;
        }
        if (metrics.errorRate < maxBenchmark.errorRate) {
            maxBenchmark.errorRate = metrics.errorRate;
        }
        if (metrics.memoryEfficiency > maxBenchmark.memoryEfficiency) {
            maxBenchmark.memoryEfficiency = metrics.memoryEfficiency;
        }
        if (metrics.cpuEfficiency > maxBenchmark.cpuEfficiency) {
            maxBenchmark.cpuEfficiency = metrics.cpuEfficiency;
        }
    }
    
    return {minBenchmark, maxBenchmark};
}

void JobMonitorService::storeMetricsSnapshot(const std::string& jobId, const JobMetrics& metrics) {
    std::scoped_lock lock(metricsHistoryMutex_);
    
    auto& history = metricsHistory_[jobId];
    history.push_back(metrics);
    
    // Limit history size
    if (history.size() > maxMetricsHistorySize_) {
        history.erase(history.begin());
    }
    
    // Cleanup old metrics periodically
    static auto lastCleanup = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    if (now - lastCleanup > std::chrono::hours(1)) { // Cleanup every hour
        cleanupOldMetrics();
        lastCleanup = now;
    }
}

JobMonitorService::ResourceUtilization JobMonitorService::getCurrentResourceUtilization() const {
    ResourceUtilization utilization;
    utilization.timestamp = std::chrono::system_clock::now();
    
    // Calculate current resource utilization from active jobs
    double totalMemory = 0.0;
    double totalCpu = 0.0;
    double peakMemory = 0.0;
    double peakCpu = 0.0;
    size_t activeJobCount = 0;
    
    withJobDataLock<void>([&]() {
        for (const auto& [jobId, data] : activeJobs_) {
            if (data.metrics.memoryUsage > 0 || data.metrics.cpuUsage > 0) {
                totalMemory += data.metrics.memoryUsage / (1024.0 * 1024.0); // Convert to MB
                totalCpu += data.metrics.cpuUsage;
                
                if (data.metrics.peakMemoryUsage / (1024.0 * 1024.0) > peakMemory) {
                    peakMemory = data.metrics.peakMemoryUsage / (1024.0 * 1024.0);
                }
                if (data.metrics.peakCpuUsage > peakCpu) {
                    peakCpu = data.metrics.peakCpuUsage;
                }
                
                activeJobCount++;
            }
        }
    });
    
    utilization.averageMemoryUsage = activeJobCount > 0 ? totalMemory / activeJobCount : 0.0;
    utilization.averageCpuUsage = activeJobCount > 0 ? totalCpu / activeJobCount : 0.0;
    utilization.peakMemoryUsage = peakMemory;
    utilization.peakCpuUsage = peakCpu;
    
    return utilization;
}

std::vector<JobMonitorService::ResourceUtilization> JobMonitorService::getResourceUtilizationHistory(
    std::chrono::system_clock::time_point since) const {
    std::scoped_lock lock(metricsHistoryMutex_);
    
    std::vector<ResourceUtilization> result;
    for (const auto& utilization : resourceHistory_) {
        if (since == std::chrono::system_clock::time_point{} || utilization.timestamp >= since) {
            result.push_back(utilization);
        }
    }
    
    return result;
}

void JobMonitorService::setMaxRecentLogs(size_t maxLogs) {
    maxRecentLogs_ = maxLogs;
    JOB_LOG_INFO("Max recent logs set to: " + std::to_string(maxLogs));
}

void JobMonitorService::setProgressUpdateThreshold(int threshold) {
    progressUpdateThreshold_ = threshold;
    JOB_LOG_INFO("Progress update threshold set to: " + std::to_string(threshold) + "%");
}

void JobMonitorService::enableNotifications(bool enabled) {
    notificationsEnabled_ = enabled;
    JOB_LOG_INFO("Notifications " + std::string(enabled ? "enabled" : "disabled"));
}

// Private helper methods

void JobMonitorService::createJobMonitoringData(const std::string& jobId) {
    std::scoped_lock lock(jobDataMutex_);
    
    if (activeJobs_.find(jobId) != activeJobs_.end()) {
        return; // Already exists
    }

    // Get job data from ETL Job Manager
    JobMonitoringData data;
    data.jobId = jobId;
    
    if (etlManager_) {
        auto job = etlManager_->getJob(jobId);
        if (job) {
            data.jobType = job->type;
            data.status = job->status;
            data.startTime = job->startedAt;
            data.createdAt = job->createdAt;
            data.completedAt = job->completedAt;
            
            data.metrics.recordsProcessed = job->recordsProcessed;
            data.metrics.recordsSuccessful = job->recordsSuccessful;
            data.metrics.recordsFailed = job->recordsFailed;
            
            if (!job->errorMessage.empty()) {
                data.errorMessage = job->errorMessage;
            }
        }
    }
    
    data.currentStep = "Job monitoring initialized";
    activeJobs_[jobId] = data;
    
    JOB_LOG_DEBUG("Created job monitoring data for job: " + jobId);
}

void JobMonitorService::updateJobMonitoringData(const std::string& jobId, 
                                              const std::function<void(JobMonitoringData&)>& updater) {
    std::scoped_lock lock(jobDataMutex_);
    
    auto activeIt = activeJobs_.find(jobId);
    if (activeIt != activeJobs_.end()) {
        updater(activeIt->second);
        return;
    }
    
    // If not in active jobs, create new monitoring data
    createJobMonitoringData(jobId);
    activeIt = activeJobs_.find(jobId);
    if (activeIt != activeJobs_.end()) {
        updater(activeIt->second);
    }
}

void JobMonitorService::moveJobToCompleted(const std::string& jobId) {
    std::scoped_lock lock(jobDataMutex_);
    
    auto activeIt = activeJobs_.find(jobId);
    if (activeIt != activeJobs_.end()) {
        completedJobs_[jobId] = activeIt->second;
        activeJobs_.erase(activeIt);
        
        JOB_LOG_DEBUG("Moved job to completed: " + jobId);
    }
}

JobStatusUpdate JobMonitorService::createJobStatusUpdate(const std::string& jobId, 
                                                       JobStatus oldStatus, 
                                                       JobStatus newStatus) {
    JobStatusUpdate update;
    update.jobId = jobId;
    update.status = newStatus;
    update.previousStatus = oldStatus;
    update.timestamp = std::chrono::system_clock::now();
    
    // Get current job data
    auto jobData = getJobMonitoringData(jobId);
    update.progressPercent = jobData.progressPercent;
    update.currentStep = jobData.currentStep;
    update.metrics = jobData.metrics;
    
    if (jobData.errorMessage.has_value()) {
        update.errorMessage = jobData.errorMessage;
    }
    
    return update;
}

WebSocketMessage JobMonitorService::createProgressMessage(const std::string& jobId, 
                                                        int progressPercent, 
                                                        const std::string& currentStep) {
    WebSocketMessage message;
    message.type = MessageType::JOB_PROGRESS_UPDATE;
    message.timestamp = std::chrono::system_clock::now();
    message.targetJobId = jobId;
    
    // Create progress data JSON
    std::ostringstream data;
    data << "{"
         << "\"jobId\":\"" << escapeJsonString(jobId) << "\","
         << "\"progressPercent\":" << progressPercent << ","
         << "\"currentStep\":\"" << escapeJsonString(currentStep) << "\","
         << "\"timestamp\":\"" << formatTimestamp(message.timestamp) << "\""
         << "}";
    
    message.data = data.str();
    return message;
}

void JobMonitorService::checkAndSendNotifications(const std::string& jobId, 
                                                JobStatus oldStatus, 
                                                JobStatus newStatus) {
    if (!notificationsEnabled_ || !notifier_) {
        return;
    }

    // Send failure notification
    if (newStatus == JobStatus::FAILED) {
        auto jobData = getJobMonitoringData(jobId);
        std::string errorMessage = jobData.errorMessage.value_or("Unknown error occurred");
        sendJobFailureNotification(jobId, errorMessage);
    }
    
    // Check for timeout warnings (jobs running longer than expected)
    if (newStatus == JobStatus::RUNNING) {
        auto jobData = getJobMonitoringData(jobId);
        if (jobData.executionTime > std::chrono::minutes(30)) { // 30 minute threshold
            sendJobTimeoutWarning(jobId, jobData.executionTime);
        }
    }
}

void JobMonitorService::sendJobFailureNotification(const std::string& jobId, const std::string& errorMessage) {
    if (notifier_) {
        notifier_->sendJobFailureAlert(jobId, errorMessage);
        JOB_LOG_INFO("Sent job failure notification for job: " + jobId);
    }
}

void JobMonitorService::sendJobTimeoutWarning(const std::string& jobId, std::chrono::milliseconds executionTime) {
    if (notifier_) {
        int executionMinutes = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(executionTime).count());
        notifier_->sendJobTimeoutWarning(jobId, executionMinutes);
        JOB_LOG_INFO("Sent job timeout warning for job: " + jobId + " (running for " + std::to_string(executionMinutes) + " minutes)");
    }
}

bool JobMonitorService::shouldUpdateProgress(const std::string& jobId, int newProgress) {
    std::scoped_lock lock(jobDataMutex_);
    
    auto activeIt = activeJobs_.find(jobId);
    if (activeIt == activeJobs_.end()) {
        return true; // Always update if job not found (first update)
    }
    
    int currentProgress = activeIt->second.progressPercent;
    return std::abs(newProgress - currentProgress) >= progressUpdateThreshold_;
}

void JobMonitorService::addLogToJob(const std::string& jobId, const std::string& logEntry) {
    updateJobMonitoringData(jobId, [&](JobMonitoringData& data) {
        data.recentLogs.push_back(logEntry);
        
        // Keep only the most recent logs
        if (data.recentLogs.size() > maxRecentLogs_) {
            data.recentLogs.erase(data.recentLogs.begin());
        }
    });
}

void JobMonitorService::cleanupOldJobs() {
    std::scoped_lock lock(jobDataMutex_);
    
    auto now = std::chrono::system_clock::now();
    auto cutoffTime = now - std::chrono::hours(24); // Keep jobs for 24 hours
    
    for (auto it = completedJobs_.begin(); it != completedJobs_.end();) {
        if (it->second.completedAt < cutoffTime) {
            JOB_LOG_DEBUG("Cleaning up old job: " + it->first);
            it = completedJobs_.erase(it);
        } else {
            ++it;
        }
    }
}

void JobMonitorService::withJobDataLock(std::function<void()> operation) const {
    std::scoped_lock lock(jobDataMutex_);
    operation();
}

// Error handling and recovery methods
bool JobMonitorService::isHealthy() const {
    return recoveryState_.isHealthy.load() && 
           circuitBreaker_.getState() != job_monitoring_recovery::ServiceCircuitBreaker::State::OPEN;
}

void JobMonitorService::setRecoveryConfig(const job_monitoring_recovery::ServiceRecoveryConfig& config) {
    recoveryConfig_ = config;
    
    if (recoveryConfig_.enableHealthChecks && running_.load() && !healthCheckRunning_.load()) {
        startHealthMonitoring();
    } else if (!recoveryConfig_.enableHealthChecks && healthCheckRunning_.load()) {
        stopHealthMonitoring();
    }
}

void JobMonitorService::performHealthCheck() {
    try {
        bool isHealthy = performComponentHealthChecks();
        
        if (isHealthy) {
            recoveryState_.failedHealthChecks.store(0);
            if (!recoveryState_.isHealthy.load()) {
                JOB_LOG_INFO("Job Monitor Service health restored");
                recoveryState_.isHealthy.store(true);
                circuitBreaker_.onSuccess();
                
                if (circuitBreaker_.isInDegradedMode()) {
                    exitDegradedMode();
                }
            }
        } else {
            recoveryState_.failedHealthChecks++;
            
            if (recoveryState_.failedHealthChecks.load() >= recoveryConfig_.maxFailedHealthChecks) {
                JOB_LOG_ERROR("Job Monitor Service health check failed " + 
                             std::to_string(recoveryState_.failedHealthChecks.load()) + " times, marking as unhealthy");
                recoveryState_.isHealthy.store(false);
                circuitBreaker_.onFailure();
                
                if (recoveryConfig_.enableGracefulDegradation) {
                    enterDegradedMode();
                }
            }
        }
        
        recoveryState_.lastHealthCheck = std::chrono::system_clock::now();
        
    } catch (const std::exception& e) {
        handleServiceError("health_check", e);
    }
}

void JobMonitorService::attemptRecovery() {
    if (!recoveryState_.shouldAttemptRecovery(recoveryConfig_)) {
        return;
    }
    
    if (recoveryState_.isRecovering.load()) {
        return; // Already attempting recovery
    }
    
    recoveryState_.isRecovering.store(true);
    recoveryState_.recoveryAttempts++;
    recoveryState_.lastRecoveryAttempt = std::chrono::system_clock::now();
    
    JOB_LOG_INFO("Attempting Job Monitor Service recovery (attempt " + 
                 std::to_string(recoveryState_.recoveryAttempts.load()) + 
                 "/" + std::to_string(recoveryConfig_.maxRecoveryAttempts) + ")");
    
    try {
        // Try to reinitialize components
        if (etlManager_ && wsManager_) {
            initialize(etlManager_, wsManager_, notifier_);
        }
        
        // Perform health check
        bool isHealthy = performComponentHealthChecks();
        
        if (isHealthy) {
            JOB_LOG_INFO("Job Monitor Service recovery successful");
            recoveryState_.reset();
            circuitBreaker_.onSuccess();
            
            // Process any queued events
            processQueuedEvents();
        } else {
            JOB_LOG_WARN("Job Monitor Service recovery attempt failed");
            circuitBreaker_.onFailure();
        }
        
    } catch (const std::exception& e) {
        JOB_LOG_ERROR("Job Monitor Service recovery attempt failed with exception: " + std::string(e.what()));
        circuitBreaker_.onFailure();
    }
    
    recoveryState_.isRecovering.store(false);
}

void JobMonitorService::handleServiceError(const std::string& operation, const std::exception& e) {
    JOB_LOG_ERROR("Job Monitor Service error in " + operation + ": " + e.what());
    
    circuitBreaker_.onFailure();
    
    if (recoveryConfig_.enableGracefulDegradation && !circuitBreaker_.isInDegradedMode()) {
        enterDegradedMode();
    }
    
    if (recoveryConfig_.enableAutoRecovery && recoveryState_.shouldAttemptRecovery(recoveryConfig_)) {
        // Schedule recovery attempt
        std::thread([this]() {
            auto delay = recoveryState_.calculateBackoffDelay(recoveryConfig_);
            std::this_thread::sleep_for(delay);
            attemptRecovery();
        }).detach();
    }
}

void JobMonitorService::enterDegradedMode() {
    JOB_LOG_WARN("Job Monitor Service entering degraded mode - basic functionality only");
    
    // In degraded mode, we still track job data but don't send real-time updates
    // Updates will be queued and sent when service recovers
}

void JobMonitorService::exitDegradedMode() {
    JOB_LOG_INFO("Job Monitor Service exiting degraded mode - full functionality restored");
    
    // Process any queued events that accumulated during degraded mode
    processQueuedEvents();
}

void JobMonitorService::processQueuedEvents() {
    try {
        // Process queued status updates
        auto statusUpdates = pendingStatusUpdates_.dequeueAll();
        for (const auto& update : statusUpdates) {
            if (wsManager_ && circuitBreaker_.allowOperation()) {
                std::string message = update.toJson();
                wsManager_->broadcastJobUpdate(message, update.jobId);
            }
        }
        
        // Process queued progress updates
        auto progressUpdates = pendingProgressUpdates_.dequeueAll();
        for (const auto& update : progressUpdates) {
            if (wsManager_ && circuitBreaker_.allowOperation()) {
                std::string message = update.toJson();
                wsManager_->broadcastMessage(message);
            }
        }
        
        if (!statusUpdates.empty() || !progressUpdates.empty()) {
            JOB_LOG_INFO("Processed " + std::to_string(statusUpdates.size()) + 
                        " status updates and " + std::to_string(progressUpdates.size()) + 
                        " progress updates from degraded mode queue");
        }
        
    } catch (const std::exception& e) {
        JOB_LOG_ERROR("Error processing queued events: " + std::string(e.what()));
    }
}

bool JobMonitorService::tryOperation(const std::function<void()>& operation, const std::string& operationName) {
    if (!circuitBreaker_.allowOperation()) {
        JOB_LOG_WARN("Circuit breaker open, skipping operation: " + operationName);
        return false;
    }
    
    try {
        operation();
        circuitBreaker_.onSuccess();
        return true;
    } catch (const std::exception& e) {
        handleServiceError(operationName, e);
        return false;
    }
}

void JobMonitorService::startHealthMonitoring() {
    if (healthCheckRunning_.load()) return;
    
    healthCheckRunning_.store(true);
    healthCheckThread_ = std::make_unique<std::thread>(&JobMonitorService::healthCheckLoop, this);
    
    JOB_LOG_INFO("Health monitoring started for Job Monitor Service");
}

void JobMonitorService::stopHealthMonitoring() {
    if (!healthCheckRunning_.load()) return;
    
    healthCheckRunning_.store(false);
    
    if (healthCheckThread_ && healthCheckThread_->joinable()) {
        healthCheckThread_->join();
    }
    
    JOB_LOG_INFO("Health monitoring stopped for Job Monitor Service");
}

void JobMonitorService::healthCheckLoop() {
    while (healthCheckRunning_.load()) {
        performHealthCheck();
        
        // Sleep for the configured interval
        std::this_thread::sleep_for(recoveryConfig_.healthCheckInterval);
    }
}

bool JobMonitorService::performComponentHealthChecks() {
    bool etlHealthy = checkETLManagerHealth();
    bool wsHealthy = checkWebSocketManagerHealth();
    bool notificationHealthy = checkNotificationServiceHealth();
    
    return etlHealthy && wsHealthy && notificationHealthy;
}

bool JobMonitorService::checkETLManagerHealth() {
    if (!etlManager_) return false;
    
    try {
        // Try to get job list to verify ETL manager is responsive
        auto jobs = etlManager_->getAllJobs();
        return true;
    } catch (const std::exception& e) {
        JOB_LOG_WARN("ETL Manager health check failed: " + std::string(e.what()));
        return false;
    }
}

bool JobMonitorService::checkWebSocketManagerHealth() {
    if (!wsManager_) return false;
    
    try {
        // Check if WebSocket manager is responsive
        auto connectionCount = wsManager_->getConnectionCount();
        return true;
    } catch (const std::exception& e) {
        JOB_LOG_WARN("WebSocket Manager health check failed: " + std::string(e.what()));
        return false;
    }
}

bool JobMonitorService::checkNotificationServiceHealth() {
    if (!notifier_) return true; // Optional component
    
    try {
        // Check if notification service is responsive
        return notifier_->isRunning();
    } catch (const std::exception& e) {
        JOB_LOG_WARN("Notification Service health check failed: " + std::string(e.what()));
        return false;
    }
}

// Metrics helper methods implementation

void JobMonitorService::cleanupOldMetrics() {
    auto cutoffTime = std::chrono::system_clock::now() - metricsRetentionPeriod_;
    
    for (auto it = metricsHistory_.begin(); it != metricsHistory_.end();) {
        auto& history = it->second;
        
        // Remove old metrics
        history.erase(
            std::remove_if(history.begin(), history.end(),
                [cutoffTime](const JobMetrics& metrics) {
                    return metrics.lastUpdateTime < cutoffTime;
                }),
            history.end());
        
        // Remove empty histories
        if (history.empty()) {
            it = metricsHistory_.erase(it);
        } else {
            ++it;
        }
    }
    
    JOB_LOG_DEBUG("Cleaned up old metrics data");
}

void JobMonitorService::cleanupOldResourceHistory() {
    auto cutoffTime = std::chrono::system_clock::now() - metricsRetentionPeriod_;
    
    resourceHistory_.erase(
        std::remove_if(resourceHistory_.begin(), resourceHistory_.end(),
            [cutoffTime](const ResourceUtilization& utilization) {
                return utilization.timestamp < cutoffTime;
            }),
        resourceHistory_.end());
    
    // Limit resource history size
    if (resourceHistory_.size() > maxResourceHistorySize_) {
        auto excess = resourceHistory_.size() - maxResourceHistorySize_;
        resourceHistory_.erase(resourceHistory_.begin(), resourceHistory_.begin() + excess);
    }
    
    JOB_LOG_DEBUG("Cleaned up old resource utilization data");
}

JobMetrics JobMonitorService::aggregateMetrics(const std::vector<JobMetrics>& metricsCollection) const {
    if (metricsCollection.empty()) {
        return JobMetrics{};
    }
    
    JobMetrics aggregated;
    size_t count = metricsCollection.size();
    
    // Sum basic metrics
    for (const auto& metrics : metricsCollection) {
        aggregated.recordsProcessed += metrics.recordsProcessed;
        aggregated.recordsSuccessful += metrics.recordsSuccessful;
        aggregated.recordsFailed += metrics.recordsFailed;
        aggregated.totalBytesProcessed += metrics.totalBytesProcessed;
        aggregated.totalBytesWritten += metrics.totalBytesWritten;
        aggregated.totalBatches += metrics.totalBatches;
        
        // Track peak values
        if (metrics.peakMemoryUsage > aggregated.peakMemoryUsage) {
            aggregated.peakMemoryUsage = metrics.peakMemoryUsage;
        }
        if (metrics.peakCpuUsage > aggregated.peakCpuUsage) {
            aggregated.peakCpuUsage = metrics.peakCpuUsage;
        }
        
        // Accumulate execution time
        aggregated.executionTime += metrics.executionTime;
    }
    
    // Calculate averages
    if (count > 0) {
        aggregated.memoryUsage = aggregated.peakMemoryUsage; // Use peak as representative
        aggregated.cpuUsage = aggregated.peakCpuUsage; // Use peak as representative
        
        // Calculate average processing rate
        double totalRate = 0.0;
        int validRates = 0;
        for (const auto& metrics : metricsCollection) {
            if (metrics.averageProcessingRate > 0) {
                totalRate += metrics.averageProcessingRate;
                validRates++;
            }
        }
        if (validRates > 0) {
            aggregated.averageProcessingRate = totalRate / validRates;
        }
        
        // Calculate average batch size
        if (aggregated.totalBatches > 0) {
            aggregated.averageBatchSize = static_cast<double>(aggregated.recordsProcessed) / aggregated.totalBatches;
        }
        
        // Calculate overall processing rate
        if (aggregated.executionTime.count() > 0) {
            aggregated.processingRate = static_cast<double>(aggregated.recordsProcessed) / 
                                      (aggregated.executionTime.count() / 1000.0);
        }
        
        // Calculate average error rate
        if (aggregated.recordsProcessed > 0) {
            aggregated.errorRate = (static_cast<double>(aggregated.recordsFailed) / aggregated.recordsProcessed) * 100.0;
        }
        
        // Calculate throughput
        if (aggregated.executionTime.count() > 0 && aggregated.totalBytesProcessed > 0) {
            double seconds = aggregated.executionTime.count() / 1000.0;
            double megabytes = aggregated.totalBytesProcessed / (1024.0 * 1024.0);
            aggregated.throughputMBps = megabytes / seconds;
        }
        
        // Calculate efficiency metrics
        if (aggregated.memoryUsage > 0) {
            double memoryMB = aggregated.memoryUsage / (1024.0 * 1024.0);
            aggregated.memoryEfficiency = aggregated.recordsProcessed / memoryMB;
        }
        
        if (aggregated.cpuUsage > 0) {
            aggregated.cpuEfficiency = aggregated.recordsProcessed / aggregated.cpuUsage;
        }
    }
    
    aggregated.lastUpdateTime = std::chrono::system_clock::now();
    
    return aggregated;
}

void JobMonitorService::updateResourceUtilization() {
    auto utilization = getCurrentResourceUtilization();
    
    std::scoped_lock lock(metricsHistoryMutex_);
    resourceHistory_.push_back(utilization);
    
    // Cleanup periodically
    static auto lastCleanup = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    if (now - lastCleanup > std::chrono::hours(1)) { // Cleanup every hour
        cleanupOldResourceHistory();
        lastCleanup = now;
    }
}

