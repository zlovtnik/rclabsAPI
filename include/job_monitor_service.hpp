#pragma once

#include "job_monitoring_models.hpp"
#include "job_monitor_service_recovery.hpp"
#include "etl_job_manager.hpp"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>

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
    
    // Health and recovery management
    bool isHealthy() const;
    void setRecoveryConfig(const job_monitoring_recovery::ServiceRecoveryConfig& config);
    const job_monitoring_recovery::ServiceRecoveryConfig& getRecoveryConfig() const { return recoveryConfig_; }
    const job_monitoring_recovery::ServiceRecoveryState& getRecoveryState() const { return recoveryState_; }
    void performHealthCheck();
    void attemptRecovery();

    // Event handling methods (called by ETL Job Manager)
    void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) override;
    void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) override;
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
    // Core components
    std::shared_ptr<ETLJobManager> etlManager_;
    std::shared_ptr<WebSocketManager> wsManager_;
    std::shared_ptr<NotificationService> notifier_;
    
    // Job data storage
    std::unordered_map<std::string, JobMonitoringData> activeJobs_;
    std::unordered_map<std::string, JobMonitoringData> completedJobs_;
    mutable std::mutex jobDataMutex_;
    
    // Service state
    std::atomic<bool> running_{false};
    std::atomic<bool> notificationsEnabled_{true};
    
    // Configuration
    int maxRecentLogs_ = 50;
    int progressUpdateThreshold_ = 5;  // Only send updates if progress changed by at least this much
    
    // Error handling and recovery
    job_monitoring_recovery::ServiceRecoveryConfig recoveryConfig_;
    job_monitoring_recovery::ServiceRecoveryState recoveryState_;
    job_monitoring_recovery::ServiceCircuitBreaker circuitBreaker_;
    
    // Degraded mode operations
    job_monitoring_recovery::DegradedModeEventQueue<JobStatusUpdate> pendingStatusUpdates_;
    job_monitoring_recovery::DegradedModeEventQueue<WebSocketMessage> pendingProgressUpdates_;
    
    // Health monitoring
    std::unique_ptr<std::thread> healthCheckThread_;
    std::atomic<bool> healthCheckRunning_{false};
    
    // Core business logic methods
    void moveJobToCompleted(const std::string& jobId);
    JobStatusUpdate createJobStatusUpdate(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus);
    WebSocketMessage createProgressMessage(const std::string& jobId, int progressPercent, const std::string& currentStep);
    void checkAndSendNotifications(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus);
    void sendJobFailureNotification(const std::string& jobId, const std::string& errorMessage);
    void sendJobTimeoutWarning(const std::string& jobId, std::chrono::milliseconds executionTime);
    bool shouldUpdateProgress(const std::string& jobId, int newProgress);
    
    // Error handling methods
    void handleServiceError(const std::string& operation, const std::exception& e);
    void enterDegradedMode();
    void exitDegradedMode();
    void processQueuedEvents();
    bool tryOperation(const std::function<void()>& operation, const std::string& operationName);
    
    // Health monitoring methods
    void startHealthMonitoring();
    void stopHealthMonitoring();
    void healthCheckLoop();
    bool performComponentHealthChecks();
    bool checkETLManagerHealth();
    bool checkWebSocketManagerHealth();
    bool checkNotificationServiceHealth();
    
    // Thread safety helper
    template<typename T>
    T withJobDataLock(std::function<T()> operation) const;
    void withJobDataLock(std::function<void()> operation) const;
    
    // Private helper methods
    void createJobMonitoringData(const std::string& jobId);
    void updateJobMonitoringData(const std::string& jobId, const std::function<void(JobMonitoringData&)>& updateFunc);
    void addLogToJob(const std::string& jobId, const std::string& logEntry);
    void cleanupOldJobs();
};

// Template implementation for thread safety helper  
template<typename T>
T JobMonitorService::withJobDataLock(std::function<T()> operation) const {
    std::scoped_lock lock(jobDataMutex_);
    return operation();
}