#include "job_monitor_service.hpp"
#include "websocket_manager.hpp"
#include "notification_service.hpp"
#include "logger.hpp"
#include <algorithm>
#include <chrono>

// NotificationService is now defined in the header file

JobMonitorService::JobMonitorService() {
    JOB_LOG_DEBUG("Job Monitor Service created");
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

    // Broadcast metrics update
    broadcastJobMetrics(jobId, metrics);
    
    JOB_LOG_DEBUG("Updated job metrics for job: " + jobId);
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
                                              std::function<void(JobMonitoringData&)> updater) {
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