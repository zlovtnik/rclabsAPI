#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <cassert>
#include "../include/etl_job_manager.hpp"
#include "../include/database_manager.hpp"
#include "../include/data_transformer.hpp"
#include "../include/logger.hpp"

// Mock JobMonitorService for testing
class MockJobMonitorService {
public:
    struct StatusChangeEvent {
        std::string jobId;
        JobStatus oldStatus;
        JobStatus newStatus;
        std::chrono::system_clock::time_point timestamp;
    };
    
    struct ProgressUpdateEvent {
        std::string jobId;
        int progressPercent;
        std::string currentStep;
        std::chrono::system_clock::time_point timestamp;
    };
    
    std::vector<StatusChangeEvent> statusChanges;
    std::vector<ProgressUpdateEvent> progressUpdates;
    
    virtual ~MockJobMonitorService() = default;
    
    virtual void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) {
        StatusChangeEvent event;
        event.jobId = jobId;
        event.oldStatus = oldStatus;
        event.newStatus = newStatus;
        event.timestamp = std::chrono::system_clock::now();
        statusChanges.push_back(event);
        
        std::cout << "Mock Monitor: Job " << jobId << " status changed from " 
                  << static_cast<int>(oldStatus) << " to " << static_cast<int>(newStatus) << std::endl;
    }
    
    virtual void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) {
        ProgressUpdateEvent event;
        event.jobId = jobId;
        event.progressPercent = progressPercent;
        event.currentStep = currentStep;
        event.timestamp = std::chrono::system_clock::now();
        progressUpdates.push_back(event);
        
        std::cout << "Mock Monitor: Job " << jobId << " progress: " 
                  << progressPercent << "% - " << currentStep << std::endl;
    }
    
    void reset() {
        statusChanges.clear();
        progressUpdates.clear();
    }
    
    // Helper methods for testing
    bool hasStatusChange(const std::string& jobId, JobStatus from, JobStatus to) const {
        for (const auto& event : statusChanges) {
            if (event.jobId == jobId && event.oldStatus == from && event.newStatus == to) {
                return true;
            }
        }
        return false;
    }
    
    bool hasProgressUpdate(const std::string& jobId, int progress, const std::string& step) const {
        for (const auto& event : progressUpdates) {
            if (event.jobId == jobId && event.progressPercent == progress && event.currentStep == step) {
                return true;
            }
        }
        return false;
    }
    
    size_t getStatusChangeCount() const { return statusChanges.size(); }
    size_t getProgressUpdateCount() const { return progressUpdates.size(); }
};

// Adapter to make MockJobMonitorService compatible with the interface
class JobMonitorService {
public:
    virtual ~JobMonitorService() = default;
    virtual void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) = 0;
    virtual void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) = 0;
};

class MockJobMonitorServiceAdapter : public JobMonitorService {
private:
    std::shared_ptr<MockJobMonitorService> mock_;
    
public:
    MockJobMonitorServiceAdapter(std::shared_ptr<MockJobMonitorService> mock) : mock_(mock) {}
    
    void onJobStatusChanged(const std::string& jobId, JobStatus oldStatus, JobStatus newStatus) override {
        mock_->onJobStatusChanged(jobId, oldStatus, newStatus);
    }
    
    void onJobProgressUpdated(const std::string& jobId, int progressPercent, const std::string& currentStep) override {
        mock_->onJobProgressUpdated(jobId, progressPercent, currentStep);
    }
};

void testJobStatusEventPublishing() {
    std::cout << "\n=== Testing Job Status Event Publishing ===" << std::endl;
    
    // Create mock services
    auto mockMonitor = std::make_shared<MockJobMonitorService>();
    auto adapter = std::make_shared<MockJobMonitorServiceAdapter>(mockMonitor);
    auto dbManager = std::make_shared<DatabaseManager>();
    auto transformer = std::make_shared<DataTransformer>();
    
    // Create ETL Job Manager and attach monitor
    ETLJobManager jobManager(dbManager, transformer);
    jobManager.setJobMonitorService(adapter);
    
    // Test direct status publishing
    std::string testJobId = "test_job_001";
    
    // Schedule a job first
    ETLJobConfig config;
    config.jobId = testJobId;
    config.type = JobType::EXTRACT;
    config.sourceConfig = "test_source";
    config.targetConfig = "test_target";
    
    std::string scheduledJobId = jobManager.scheduleJob(config);
    assert(scheduledJobId == testJobId);
    
    // Test manual status publishing
    jobManager.publishJobStatusUpdate(testJobId, JobStatus::RUNNING);
    jobManager.publishJobStatusUpdate(testJobId, JobStatus::COMPLETED);
    
    // Verify events were captured
    assert(mockMonitor->getStatusChangeCount() >= 2);
    assert(mockMonitor->hasStatusChange(testJobId, JobStatus::PENDING, JobStatus::RUNNING));
    assert(mockMonitor->hasStatusChange(testJobId, JobStatus::RUNNING, JobStatus::COMPLETED));
    
    std::cout << "✓ Job status event publishing test passed" << std::endl;
}

void testJobProgressEventPublishing() {
    std::cout << "\n=== Testing Job Progress Event Publishing ===" << std::endl;
    
    // Create mock services
    auto mockMonitor = std::make_shared<MockJobMonitorService>();
    auto adapter = std::make_shared<MockJobMonitorServiceAdapter>(mockMonitor);
    auto dbManager = std::make_shared<DatabaseManager>();
    auto transformer = std::make_shared<DataTransformer>();
    
    // Create ETL Job Manager and attach monitor
    ETLJobManager jobManager(dbManager, transformer);
    jobManager.setJobMonitorService(adapter);
    
    std::string testJobId = "test_job_002";
    
    // Test progress publishing
    jobManager.publishJobProgress(testJobId, 0, "Starting job");
    jobManager.publishJobProgress(testJobId, 25, "Processing batch 1");
    jobManager.publishJobProgress(testJobId, 50, "Processing batch 2");
    jobManager.publishJobProgress(testJobId, 75, "Processing batch 3");
    jobManager.publishJobProgress(testJobId, 100, "Job completed");
    
    // Verify progress events were captured
    assert(mockMonitor->getProgressUpdateCount() == 5);
    assert(mockMonitor->hasProgressUpdate(testJobId, 0, "Starting job"));
    assert(mockMonitor->hasProgressUpdate(testJobId, 25, "Processing batch 1"));
    assert(mockMonitor->hasProgressUpdate(testJobId, 50, "Processing batch 2"));
    assert(mockMonitor->hasProgressUpdate(testJobId, 75, "Processing batch 3"));
    assert(mockMonitor->hasProgressUpdate(testJobId, 100, "Job completed"));
    
    std::cout << "✓ Job progress event publishing test passed" << std::endl;
}

void testJobExecutionWithMonitoring() {
    std::cout << "\n=== Testing Job Execution with Monitoring ===" << std::endl;
    
    // Create mock services
    auto mockMonitor = std::make_shared<MockJobMonitorService>();
    auto adapter = std::make_shared<MockJobMonitorServiceAdapter>(mockMonitor);
    auto dbManager = std::make_shared<DatabaseManager>();
    auto transformer = std::make_shared<DataTransformer>();
    
    // Initialize database connection for testing
    ConnectionConfig dbConfig;
    dbConfig.host = "localhost";
    dbConfig.port = 1521;
    dbConfig.database = "test_db";
    dbConfig.username = "test_user";
    dbConfig.password = "test_pass";
    dbManager->connect(dbConfig);
    
    // Create ETL Job Manager and attach monitor
    ETLJobManager jobManager(dbManager, transformer);
    jobManager.setJobMonitorService(adapter);
    jobManager.start();
    
    // Schedule a simple extract job
    ETLJobConfig extractConfig;
    extractConfig.type = JobType::EXTRACT;
    extractConfig.sourceConfig = "test_source";
    extractConfig.targetConfig = "test_target";
    
    std::string extractJobId = jobManager.scheduleJob(extractConfig);
    
    // Wait for job to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Verify the job went through proper status transitions
    assert(mockMonitor->hasStatusChange(extractJobId, JobStatus::PENDING, JobStatus::RUNNING));
    assert(mockMonitor->hasStatusChange(extractJobId, JobStatus::RUNNING, JobStatus::COMPLETED));
    
    // Verify progress updates were sent
    assert(mockMonitor->hasProgressUpdate(extractJobId, 0, "Starting data extraction"));
    assert(mockMonitor->hasProgressUpdate(extractJobId, 100, "Data extraction completed"));
    
    mockMonitor->reset();
    
    // Schedule a full ETL job to test detailed progress tracking
    ETLJobConfig fullETLConfig;
    fullETLConfig.type = JobType::FULL_ETL;
    fullETLConfig.sourceConfig = "test_source";
    fullETLConfig.targetConfig = "test_target";
    
    std::string fullETLJobId = jobManager.scheduleJob(fullETLConfig);
    
    // Wait for job to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Verify detailed progress tracking for full ETL
    assert(mockMonitor->hasProgressUpdate(fullETLJobId, 0, "Starting full ETL pipeline"));
    assert(mockMonitor->hasProgressUpdate(fullETLJobId, 10, "Extracting data from source"));
    assert(mockMonitor->hasProgressUpdate(fullETLJobId, 50, "Transforming extracted data"));
    assert(mockMonitor->hasProgressUpdate(fullETLJobId, 80, "Loading transformed data"));
    assert(mockMonitor->hasProgressUpdate(fullETLJobId, 100, "Full ETL pipeline completed"));
    
    // Verify status transitions
    assert(mockMonitor->hasStatusChange(fullETLJobId, JobStatus::PENDING, JobStatus::RUNNING));
    assert(mockMonitor->hasStatusChange(fullETLJobId, JobStatus::RUNNING, JobStatus::COMPLETED));
    
    jobManager.stop();
    
    std::cout << "✓ Job execution with monitoring test passed" << std::endl;
}

void testJobExecutionWithoutMonitoring() {
    std::cout << "\n=== Testing Job Execution without Monitoring ===" << std::endl;
    
    // Create services without monitor
    auto dbManager = std::make_shared<DatabaseManager>();
    auto transformer = std::make_shared<DataTransformer>();
    
    // Initialize database connection for testing
    ConnectionConfig dbConfig;
    dbConfig.host = "localhost";
    dbConfig.port = 1521;
    dbConfig.database = "test_db";
    dbConfig.username = "test_user";
    dbConfig.password = "test_pass";
    dbManager->connect(dbConfig);
    
    // Create ETL Job Manager without monitor service
    ETLJobManager jobManager(dbManager, transformer);
    jobManager.start();
    
    // Schedule a job
    ETLJobConfig jobConfig;
    jobConfig.type = JobType::EXTRACT;
    jobConfig.sourceConfig = "test_source";
    jobConfig.targetConfig = "test_target";
    
    std::string jobId = jobManager.scheduleJob(jobConfig);
    
    // Wait for job to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Verify job completed successfully even without monitoring
    auto job = jobManager.getJob(jobId);
    assert(job != nullptr);
    assert(job->status == JobStatus::COMPLETED);
    
    jobManager.stop();
    
    std::cout << "✓ Job execution without monitoring test passed" << std::endl;
}

void testMonitorServiceIntegration() {
    std::cout << "\n=== Testing Monitor Service Integration ===" << std::endl;
    
    // Create mock services
    auto mockMonitor = std::make_shared<MockJobMonitorService>();
    auto adapter = std::make_shared<MockJobMonitorServiceAdapter>(mockMonitor);
    auto dbManager = std::make_shared<DatabaseManager>();
    auto transformer = std::make_shared<DataTransformer>();
    
    // Create ETL Job Manager
    ETLJobManager jobManager(dbManager, transformer);
    
    // Test setting monitor service
    jobManager.setJobMonitorService(adapter);
    
    // Test that monitor service is properly integrated
    std::string testJobId = "integration_test_job";
    
    // Schedule a job
    ETLJobConfig config;
    config.jobId = testJobId;
    config.type = JobType::TRANSFORM;
    config.sourceConfig = "test_source";
    config.targetConfig = "test_target";
    
    jobManager.scheduleJob(config);
    
    // Test manual event publishing
    jobManager.publishJobStatusUpdate(testJobId, JobStatus::RUNNING);
    jobManager.publishJobProgress(testJobId, 50, "Halfway through transformation");
    jobManager.publishJobStatusUpdate(testJobId, JobStatus::COMPLETED);
    
    // Verify events were received
    assert(mockMonitor->getStatusChangeCount() >= 2);
    assert(mockMonitor->getProgressUpdateCount() >= 1);
    
    std::cout << "✓ Monitor service integration test passed" << std::endl;
}

int main() {
    std::cout << "Starting ETL Job Manager Monitoring Tests..." << std::endl;
    
    try {
        // Initialize logger for testing
        LogConfig logConfig;
        logConfig.level = LogLevel::DEBUG;
        logConfig.logFile = "logs/test_etl_monitoring.log";
        logConfig.fileOutput = false; // Disable file output for testing
        logConfig.consoleOutput = true;
        Logger::getInstance().configure(logConfig);
        
        testJobStatusEventPublishing();
        testJobProgressEventPublishing();
        testJobExecutionWithMonitoring();
        testJobExecutionWithoutMonitoring();
        testMonitorServiceIntegration();
        
        std::cout << "\n🎉 All ETL Job Manager monitoring tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}