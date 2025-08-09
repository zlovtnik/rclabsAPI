#pragma once

#include "job_monitoring_models.hpp"
#include "etl_job_manager.hpp"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>

// Forward declarations
class WebSocketManager;
class NotificationService;  // Forward declaration - full definition in notification_service.hpp

// Forward declaration to match ETL Job Manager's interface
class JobMonitorServiceInterface {
public:
    virtual ~JobMonitorServiceInterface() = default;
    virtual void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) = 0;
    virtual void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) = 0;
};

/**
 * JobMonitorService serves as the central coordination component for real-time job monitoring.
 * It aggregates job status information, handles events from ETL Job Manager, and distributes
 * updates to WebSocket clients and notification services.
 */
class JobMonitorService : public JobMonitorServiceInterface {
public:
    JobMonitorService();
    ~JobMonitorService();

    // Initialization and lifecycle
    void initialize(std::shared_ptr<ETLJobManager> etlManager,
                   std::shared_ptr<WebSocketManager> wsManager,
                   std::shared_ptr<NotificationService> notifier = nullptr);
    void start();
    void stop();
    bool isRunning() const;

    // Event handling methods (called by ETL Job Manager)
    void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus);
    void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep);
    void onJobLogGenerated(const std::string& jobId, const LogMessage& logMessage);

    // Job data access methods
    JobMonitoringData getJobMonitoringData(const std::string& jobId);
    std::vector<JobMonitoringData> getAllActiveJobs();
    std::vector<JobMonitoringData> getJobsByStatus(JobStatus status);
    std::vector<JobMonitoringData> getJobsByType(JobType type);

    // WebSocket message formatting and distribution
    void broadcastJobStatusUpdate(const JobStatusUpdate& update);
    void broadcastJobProgress(const std::string& jobId, int progressPercent, const std::string& currentStep);
    void broadcastLogMessage(const LogMessage& logMessage);
    void broadcastJobMetrics(const std::string& jobId, const JobMetrics& metrics);

    // Active job tracking
    size_t getActiveJobCount() const;
    std::vector<std::string> getActiveJobIds() const;
    bool isJobActive(const std::string& jobId) const;

    // Job metrics and statistics
    JobMetrics getJobMetrics(const std::string& jobId) const;
    void updateJobMetrics(const std::string& jobId, const JobMetrics& metrics);

    // Configuration and settings
    void setMaxRecentLogs(size_t maxLogs);
    void setProgressUpdateThreshold(int threshold);
    void enableNotifications(bool enabled);

private:
    // Core dependencies
    std::shared_ptr<ETLJobManager> etlManager_;
    std::shared_ptr<WebSocketManager> wsManager_;
    std::shared_ptr<NotificationService> notifier_;

    // Job monitoring data storage
    mutable std::mutex jobDataMutex_;
    std::unordered_map<std::string, JobMonitoringData> activeJobs_;
    std::unordered_map<std::string, JobMonitoringData> completedJobs_;

    // Configuration
    size_t maxRecentLogs_ = 50;
    int progressUpdateThreshold_ = 5; // Minimum progress change to trigger update
    bool notificationsEnabled_ = true;
    bool running_ = false;

    // Internal helper methods
    void createJobMonitoringData(const std::string& jobId);
    void updateJobMonitoringData(const std::string& jobId, 
                               std::function<void(JobMonitoringData&)> updater);
    void moveJobToCompleted(const std::string& jobId);
    
    // Message creation helpers
    JobStatusUpdate createJobStatusUpdate(const std::string& jobId, 
                                        JobStatus oldStatus, 
                                        JobStatus newStatus);
    WebSocketMessage createProgressMessage(const std::string& jobId, 
                                         int progressPercent, 
                                         const std::string& currentStep);
    
    // Notification helpers
    void checkAndSendNotifications(const std::string& jobId, 
                                 JobStatus oldStatus, 
                                 JobStatus newStatus);
    void sendJobFailureNotification(const std::string& jobId, const std::string& errorMessage);
    void sendJobTimeoutWarning(const std::string& jobId, std::chrono::milliseconds executionTime);

    // Utility methods
    bool shouldUpdateProgress(const std::string& jobId, int newProgress);
    void addLogToJob(const std::string& jobId, const std::string& logEntry);
    void cleanupOldJobs();
    
    // Thread safety helpers
    template<typename T>
    T withJobDataLock(std::function<T()> operation) const;
    void withJobDataLock(std::function<void()> operation) const;
};

// Template implementation for thread safety helper
template<typename T>
T JobMonitorService::withJobDataLock(std::function<T()> operation) const {
    std::lock_guard<std::mutex> lock(jobDataMutex_);
    return operation();
}