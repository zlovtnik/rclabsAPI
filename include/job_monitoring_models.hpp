#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>
#include <functional>
#include "etl_job_manager.hpp"
#include "transparent_string_hash.hpp"

// Forward declarations
struct LogMessage;

// Message type enumeration for WebSocket routing
enum class MessageType {
    JOB_STATUS_UPDATE,
    JOB_PROGRESS_UPDATE,
    JOB_LOG_MESSAGE,
    JOB_METRICS_UPDATE,
    SYSTEM_NOTIFICATION,
    CONNECTION_ACK,
    ERROR_MESSAGE
};

// Job execution metrics
struct JobMetrics {
    int recordsProcessed = 0;
    int recordsSuccessful = 0;
    int recordsFailed = 0;
    double processingRate = 0.0; // records per second
    size_t memoryUsage = 0;      // bytes
    double cpuUsage = 0.0;       // percentage
    std::chrono::milliseconds executionTime{0};
    
    // JSON serialization
    std::string toJson() const;
    static JobMetrics fromJson(const std::string& json);
    
    // Helper methods
    void updateProcessingRate(std::chrono::milliseconds elapsed);
    void reset();
};

// Job status update message for WebSocket communication
struct JobStatusUpdate {
    std::string jobId;
    JobStatus status;
    JobStatus previousStatus;
    std::chrono::system_clock::time_point timestamp;
    int progressPercent = 0;
    std::string currentStep;
    std::optional<std::string> errorMessage;
    JobMetrics metrics;
    
    // JSON serialization
    std::string toJson() const;
    static JobStatusUpdate fromJson(const std::string& json);
    
    // Helper methods
    bool isStatusChange() const;
    bool isProgressUpdate() const;
};

// Comprehensive job monitoring data
struct JobMonitoringData {
    std::string jobId;
    JobType jobType;
    JobStatus status;
    int progressPercent = 0;
    std::string currentStep;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point completedAt;
    std::chrono::milliseconds executionTime{0};
    JobMetrics metrics;
    std::vector<std::string> recentLogs; // Last N log entries
    std::optional<std::string> errorMessage;
    
    // JSON serialization
    std::string toJson() const;
    static JobMonitoringData fromJson(const std::string& json);
    
    // Helper methods
    void updateExecutionTime();
    bool isActive() const;
    std::string getStatusString() const;
    std::string getJobTypeString() const;
};

// Log message structure for real-time streaming
struct LogMessage {
    std::string jobId;
    std::string level;
    std::string component;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> context;
    
    // JSON serialization
    std::string toJson() const;
    static LogMessage fromJson(const std::string& json);
    
    // Helper methods
    bool matchesFilter(const std::string& jobIdFilter, const std::string& levelFilter) const;
};

// WebSocket message wrapper for routing
struct WebSocketMessage {
    MessageType type;
    std::chrono::system_clock::time_point timestamp;
    std::string data; // JSON payload
    std::optional<std::string> targetJobId; // For filtering
    std::optional<std::string> targetLevel; // For log filtering
    
    // JSON serialization
    std::string toJson() const;
    static WebSocketMessage fromJson(const std::string& json);
    
    // Factory methods for different message types
    static WebSocketMessage createJobStatusUpdate(const JobStatusUpdate& update);
    static WebSocketMessage createLogMessage(const LogMessage& logMsg);
    static WebSocketMessage createMetricsUpdate(const std::string& jobId, const JobMetrics& metrics);
    static WebSocketMessage createErrorMessage(const std::string& error);
    static WebSocketMessage createConnectionAck();
};

// Connection filter preferences
struct ConnectionFilters {
    std::vector<std::string> jobIds;        // Empty = all jobs
    std::vector<std::string> logLevels;     // Empty = all levels
    std::vector<MessageType> messageTypes;  // Empty = all types
    bool includeSystemNotifications = true;
    
    // JSON serialization
    std::string toJson() const;
    static ConnectionFilters fromJson(const std::string& json);
    
    // Helper methods
    bool shouldReceiveMessage(const WebSocketMessage& message) const;
    bool shouldReceiveJob(const std::string& jobId) const;
    bool shouldReceiveLogLevel(const std::string& level) const;
    bool shouldReceiveMessageType(MessageType type) const;
    
    // Enhanced filtering methods
    void addJobId(const std::string& jobId);
    void removeJobId(const std::string& jobId);
    void addMessageType(MessageType messageType);
    void removeMessageType(MessageType messageType);
    void addLogLevel(const std::string& logLevel);
    void removeLogLevel(const std::string& logLevel);
    void clear();
    
    // Filter information
    bool hasFilters() const;
    bool hasJobFilters() const;
    bool hasMessageTypeFilters() const;
    bool hasLogLevelFilters() const;
    size_t getTotalFilterCount() const;
    
    // Filter validation
    bool isValid() const;
    std::string getValidationErrors() const;
};

// Utility functions for message type conversion
std::string messageTypeToString(MessageType type);
MessageType stringToMessageType(const std::string& typeStr);

// Utility functions for job status/type conversion
std::string jobStatusToString(JobStatus status);
JobStatus stringToJobStatus(const std::string& statusStr);
std::string jobTypeToString(JobType type);
JobType stringToJobType(const std::string& typeStr);

// JSON utility functions
std::string escapeJsonString(const std::string& str);
std::string formatTimestamp(const std::chrono::system_clock::time_point& timePoint);
std::chrono::system_clock::time_point parseTimestamp(const std::string& timestampStr);

// Validation functions
bool validateJobId(const std::string& jobId);
bool validateLogLevel(const std::string& level);
bool validateMessageType(const std::string& typeStr);