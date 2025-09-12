#include "config_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "job_monitor_service.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include "websocket_manager.hpp"
#include <cassert>
#include <iostream>

// Simple NotificationService for testing
class TestNotificationService : public NotificationService {
public:
  void sendJobFailureAlert(const std::string &jobId,
                           const std::string &error) override {
    std::cout << "NOTIFICATION: Job failure alert for " << jobId << " - "
              << error << std::endl;
    failureCount++;
  }

  void sendJobTimeoutWarning(const std::string &jobId,
                             int executionTimeMinutes) override {
    std::cout << "NOTIFICATION: Job timeout warning for " << jobId << " - "
              << executionTimeMinutes << " minutes" << std::endl;
    timeoutCount++;
  }

  bool isRunning() const override { return true; }

  int failureCount = 0;
  int timeoutCount = 0;
};

int main() {
  try {
    std::cout << "=== Simple Job Monitor Service Test ===" << std::endl;

    // Configure logger
    LogConfig logConfig;
    logConfig.level = LogLevel::INFO;
    logConfig.consoleOutput = true;
    Logger::getInstance().configure(logConfig);

    // Initialize components
    auto dbManager = std::make_shared<DatabaseManager>();
    auto transformer = std::make_shared<DataTransformer>();
    auto etlManager = std::make_shared<ETLJobManager>(dbManager, transformer);
    auto wsManager = std::make_shared<WebSocketManager>();
    auto notificationService = std::make_shared<TestNotificationService>();
    auto jobMonitorService = std::make_shared<JobMonitorService>();

    // Test initialization
    std::cout << "\n1. Testing initialization..." << std::endl;
    jobMonitorService->initialize(etlManager, wsManager, notificationService);
    jobMonitorService->start();
    assert(jobMonitorService->isRunning());
    std::cout << "✓ JobMonitorService initialized and started successfully"
              << std::endl;

    // Test job status changes
    std::cout << "\n2. Testing job status changes..." << std::endl;
    std::string testJobId = "simple_test_job_001";

    jobMonitorService->onJobStatusChanged(testJobId, JobStatus::PENDING,
                                          JobStatus::RUNNING);
    assert(jobMonitorService->isJobActive(testJobId));
    std::cout << "✓ Job correctly tracked as active" << std::endl;

    // Test job progress updates
    std::cout << "\n3. Testing job progress updates..." << std::endl;
    jobMonitorService->onJobProgressUpdated(testJobId, 50, "Processing data");

    auto jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.progressPercent == 50);
    assert(jobData.currentStep == "Processing data");
    std::cout << "✓ Job progress updated correctly" << std::endl;

    // Test job completion
    std::cout << "\n4. Testing job completion..." << std::endl;
    jobMonitorService->onJobStatusChanged(testJobId, JobStatus::RUNNING,
                                          JobStatus::COMPLETED);
    assert(!jobMonitorService->isJobActive(testJobId));

    jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.status == JobStatus::COMPLETED);
    std::cout << "✓ Job completion handled correctly" << std::endl;

    // Test job failure with notification
    std::cout << "\n5. Testing job failure notification..." << std::endl;
    std::string failedJobId = "failed_job_001";
    jobMonitorService->onJobStatusChanged(failedJobId, JobStatus::RUNNING,
                                          JobStatus::FAILED);

    // Check notification was sent
    assert(notificationService->failureCount > 0);
    std::cout << "✓ Job failure notification sent successfully" << std::endl;

    // Test metrics
    std::cout << "\n6. Testing job metrics..." << std::endl;
    JobMetrics testMetrics;
    testMetrics.recordsProcessed = 1000;
    testMetrics.recordsSuccessful = 950;
    testMetrics.recordsFailed = 50;

    jobMonitorService->updateJobMetrics(testJobId, testMetrics);
    auto retrievedMetrics = jobMonitorService->getJobMetrics(testJobId);
    assert(retrievedMetrics.recordsProcessed == 1000);
    std::cout << "✓ Job metrics updated and retrieved correctly" << std::endl;

    // Test active job tracking
    std::cout << "\n7. Testing active job tracking..." << std::endl;
    std::string activeJobId = "active_job_001";
    jobMonitorService->onJobStatusChanged(activeJobId, JobStatus::PENDING,
                                          JobStatus::RUNNING);

    auto activeJobs = jobMonitorService->getAllActiveJobs();
    bool foundActiveJob = false;
    for (const auto &job : activeJobs) {
      if (job.jobId == activeJobId) {
        foundActiveJob = true;
        break;
      }
    }
    assert(foundActiveJob);
    std::cout << "✓ Active job tracking working correctly" << std::endl;

    // Test configuration
    std::cout << "\n8. Testing configuration..." << std::endl;
    jobMonitorService->setMaxRecentLogs(100);
    jobMonitorService->setProgressUpdateThreshold(10);
    jobMonitorService->enableNotifications(false);
    jobMonitorService->enableNotifications(true);
    std::cout << "✓ Configuration methods working correctly" << std::endl;

    // Clean up
    jobMonitorService->stop();
    assert(!jobMonitorService->isRunning());
    std::cout << "✓ JobMonitorService stopped successfully" << std::endl;

    std::cout << "\n🎉 All JobMonitorService tests passed!" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Test failed with unknown exception" << std::endl;
    return 1;
  }
}