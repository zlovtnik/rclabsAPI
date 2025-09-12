#include "config_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "job_monitor_service.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include "websocket_manager.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

// Mock NotificationService for testing
class MockNotificationService : public NotificationService {
public:
  /**
 * @brief Virtual destructor for MockNotificationService.
 *
 * Ensures proper polymorphic destruction of MockNotificationService instances and
 * allows derived classes to clean up resources correctly.
 */
virtual ~MockNotificationService() = default;

  /**
   * @brief Mock implementation that records and reports a job failure alert.
   *
   * Logs a failure message to stdout and appends a FailureAlert entry to the
   * MockNotificationService::failureAlerts vector for later inspection in tests.
   *
   * @param jobId Identifier of the failed job.
   * @param error Human-readable error description or message associated with the failure.
   */
  virtual void sendJobFailureAlert(const std::string &jobId,
                                   const std::string &error) override {
    std::cout << "NOTIFICATION: Job failure alert for " << jobId << " - "
              << error << std::endl;
    failureAlerts.push_back({jobId, error});
  }

  /**
   * @brief Record and emit a timeout warning for a job.
   *
   * Logs a timeout warning to standard output and appends a TimeoutWarning
   * entry (containing the job ID and execution time in minutes) to the
   * MockNotificationService's internal timeoutWarnings vector.
   *
   * @param jobId Identifier of the job that triggered the warning.
   * @param executionTimeMinutes Execution time in minutes used in the warning.
   */
  virtual void sendJobTimeoutWarning(const std::string &jobId,
                                     int executionTimeMinutes) override {
    std::cout << "NOTIFICATION: Job timeout warning for " << jobId << " - "
              << executionTimeMinutes << " minutes" << std::endl;
    timeoutWarnings.push_back({jobId, executionTimeMinutes});
  }

  /**
 * @brief Always reports the mock notification service as running.
 *
 * Overrides NotificationService::isRunning to unconditionally return true so
 * tests treat the mock as operational regardless of internal state.
 *
 * @return true Always returns true.
 */
bool isRunning() const override { return true; }

  struct FailureAlert {
    std::string jobId;
    std::string error;
  };

  struct TimeoutWarning {
    std::string jobId;
    int executionTimeMinutes;
  };

  std::vector<FailureAlert> failureAlerts;
  std::vector<TimeoutWarning> timeoutWarnings;
};

class JobMonitorServiceTest {
public:
  /**
   * @brief Constructs the test harness and wires together all test dependencies.
   *
   * Initializes logging, loads test configuration, and creates the core components
   * used across the test suite: DatabaseManager, DataTransformer, ETLJobManager,
   * WebSocketManager, a MockNotificationService, and the JobMonitorService under
   * test. The logger is configured to write to "logs/test_job_monitor_service.log".
   */
  JobMonitorServiceTest() {
    // Configure logger
    LogConfig logConfig;
    logConfig.level = LogLevel::DEBUG;
    logConfig.consoleOutput = true;
    logConfig.fileOutput = true;
    logConfig.logFile = "logs/test_job_monitor_service.log";
    Logger::getInstance().configure(logConfig);

    // Initialize config manager (singleton)
    ConfigManager::getInstance().loadConfig("config.json");

    // Initialize database manager
    dbManager = std::make_shared<DatabaseManager>();

    // Initialize data transformer
    transformer = std::make_shared<DataTransformer>();

    // Initialize ETL Job Manager
    etlManager = std::make_shared<ETLJobManager>(dbManager, transformer);

    // Initialize WebSocket Manager
    wsManager = std::make_shared<WebSocketManager>();

    // Initialize mock notification service
    notificationService = std::make_shared<MockNotificationService>();

    // Initialize Job Monitor Service
    jobMonitorService = std::make_shared<JobMonitorService>();
  }

  /**
   * @brief Executes the full suite of Job Monitor Service unit tests.
   *
   * Runs all test cases in a fixed sequence:
   * initialization, status transitions, progress handling, data retrieval,
   * active-job tracking, WebSocket broadcasting, metrics, notification
   * integration, configuration settings, and error handling. Prints start
   * and completion markers to stdout.
   */
  void runAllTests() {
    std::cout << "\n=== Job Monitor Service Tests ===" << std::endl;

    testInitialization();
    testJobStatusChangeHandling();
    testJobProgressUpdates();
    testJobDataRetrieval();
    testActiveJobTracking();
    testWebSocketMessageBroadcasting();
    testJobMetricsHandling();
    testNotificationIntegration();
    testConfigurationSettings();
    testErrorHandling();

    std::cout << "\n=== All Job Monitor Service Tests Completed ==="
              << std::endl;
  }

private:
  std::shared_ptr<DatabaseManager> dbManager;
  std::shared_ptr<DataTransformer> transformer;
  std::shared_ptr<ETLJobManager> etlManager;
  std::shared_ptr<WebSocketManager> wsManager;
  std::shared_ptr<NotificationService> notificationService;
  std::shared_ptr<JobMonitorService> jobMonitorService;

  void testInitialization() {
    std::cout << "\n--- Test: Initialization ---" << std::endl;

    // Test initialization with required components
    try {
      jobMonitorService->initialize(etlManager, wsManager, notificationService);
      std::cout << "✓ Job Monitor Service initialized successfully"
                << std::endl;
    } catch (const std::exception &e) {
      std::cout << "✗ Initialization failed: " << e.what() << std::endl;
      return;
    }

    // Test starting the service
    jobMonitorService->start();
    assert(jobMonitorService->isRunning());
    std::cout << "✓ Job Monitor Service started successfully" << std::endl;

    // Test initialization with null parameters
    auto testService = std::make_shared<JobMonitorService>();
    try {
      testService->initialize(nullptr, wsManager);
      std::cout << "✗ Should have thrown exception for null ETL manager"
                << std::endl;
    } catch (const std::invalid_argument &e) {
      std::cout << "✓ Correctly rejected null ETL manager" << std::endl;
    }

    try {
      testService->initialize(etlManager, nullptr);
      std::cout << "✗ Should have thrown exception for null WebSocket manager"
                << std::endl;
    } catch (const std::invalid_argument &e) {
      std::cout << "✓ Correctly rejected null WebSocket manager" << std::endl;
    }
  }

  /**
   * @brief Runs unit tests for job status transition handling in the JobMonitorService.
   *
   * Executes a sequence of status-change scenarios and asserts that the JobMonitorService
   * updates its internal tracking and monitoring data accordingly:
   * - PENDING -> RUNNING: job becomes active and monitoring data is created/updated with RUNNING status.
   * - RUNNING -> COMPLETED: job is removed from active tracking but its monitoring data remains with COMPLETED status.
   * - RUNNING -> FAILED: monitoring data reflects FAILED status.
   *
   * Uses assertions to validate expected outcomes and prints pass messages for each subtest.
   */
  void testJobStatusChangeHandling() {
    std::cout << "\n--- Test: Job Status Change Handling ---" << std::endl;

    std::string testJobId = "test_job_status_001";

    // Test status change from PENDING to RUNNING
    jobMonitorService->onJobStatusChanged(testJobId, JobStatus::PENDING,
                                          JobStatus::RUNNING);

    // Verify job is now tracked as active
    assert(jobMonitorService->isJobActive(testJobId));
    std::cout
        << "✓ Job correctly tracked as active after status change to RUNNING"
        << std::endl;

    // Get job monitoring data
    auto jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.jobId == testJobId);
    assert(jobData.status == JobStatus::RUNNING);
    std::cout << "✓ Job monitoring data correctly created and updated"
              << std::endl;

    // Test status change to COMPLETED
    jobMonitorService->onJobStatusChanged(testJobId, JobStatus::RUNNING,
                                          JobStatus::COMPLETED);

    // Verify job is no longer active but still retrievable
    assert(!jobMonitorService->isJobActive(testJobId));
    jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.status == JobStatus::COMPLETED);
    std::cout << "✓ Job correctly moved to completed after status change"
              << std::endl;

    // Test status change to FAILED
    std::string failedJobId = "test_job_failed_001";
    jobMonitorService->onJobStatusChanged(failedJobId, JobStatus::RUNNING,
                                          JobStatus::FAILED);

    jobData = jobMonitorService->getJobMonitoringData(failedJobId);
    assert(jobData.status == JobStatus::FAILED);
    std::cout << "✓ Failed job status correctly handled" << std::endl;
  }

  /**
   * @brief Runs unit tests validating job progress update handling.
   *
   * Executes a sequence of progress-update scenarios against the shared
   * JobMonitorService instance:
   * - Initializes a test job and transitions it to RUNNING.
   * - Sends progress updates and verifies stored progress percentage and current step.
   * - Sets a progress-update threshold and verifies that small changes below the
   *   threshold are ignored.
   * - Sends a larger update and verifies it is applied.
   *
   * The function uses assertions to validate expected state and prints brief
   * pass/fail markers to stdout. It mutates the monitor service state (including
   * the progress update threshold) and relies on the member `jobMonitorService`.
   */
  void testJobProgressUpdates() {
    std::cout << "\n--- Test: Job Progress Updates ---" << std::endl;

    std::string testJobId = "test_job_progress_001";

    // Initialize job
    jobMonitorService->onJobStatusChanged(testJobId, JobStatus::PENDING,
                                          JobStatus::RUNNING);

    // Test progress updates
    jobMonitorService->onJobProgressUpdated(testJobId, 25,
                                            "Processing batch 1/4");

    auto jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.progressPercent == 25);
    assert(jobData.currentStep == "Processing batch 1/4");
    std::cout << "✓ Job progress correctly updated (25%)" << std::endl;

    // Test another progress update
    jobMonitorService->onJobProgressUpdated(testJobId, 75,
                                            "Processing batch 3/4");

    jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.progressPercent == 75);
    assert(jobData.currentStep == "Processing batch 3/4");
    std::cout << "✓ Job progress correctly updated (75%)" << std::endl;

    // Test progress update threshold (should not update for small changes)
    jobMonitorService->setProgressUpdateThreshold(10);
    jobMonitorService->onJobProgressUpdated(testJobId, 77, "Minor progress");

    jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.progressPercent == 75); // Should not have changed
    std::cout << "✓ Progress update threshold correctly applied" << std::endl;

    // Test significant progress update (should update)
    jobMonitorService->onJobProgressUpdated(testJobId, 90, "Almost complete");

    jobData = jobMonitorService->getJobMonitoringData(testJobId);
    assert(jobData.progressPercent == 90);
    assert(jobData.currentStep == "Almost complete");
    std::cout << "✓ Significant progress update correctly processed"
              << std::endl;
  }

  /**
   * @brief Tests retrieval and querying of job monitoring data.
   *
   * This test creates multiple jobs with different lifecycle states, exercises
   * JobMonitorService query APIs, and asserts expected results. Specifically:
   * - Transitions three jobs through PENDING/RUNNING/COMPLETED states.
   * - Verifies getAllActiveJobs returns the active/running jobs.
   * - Verifies getJobsByStatus returns expected counts for RUNNING and COMPLETED.
   * - Verifies getJobMonitoringData returns valid monitoring records for each
   *   created job.
   * - Verifies retrieval of a non-existent job returns a record whose jobId
   *   matches the requested id (handled as a non-populated/placeholder entry).
   *
   * Side effects:
   * - Mutates the shared jobMonitorService state by invoking onJobStatusChanged.
   * - Uses assertions to fail the test when expectations are not met.
   *
   * Notes:
   * - Relies on the test fixture's jobMonitorService instance being initialized.
   */
  void testJobDataRetrieval() {
    std::cout << "\n--- Test: Job Data Retrieval ---" << std::endl;

    // Create multiple test jobs
    std::vector<std::string> jobIds = {"retrieve_job_001", "retrieve_job_002",
                                       "retrieve_job_003"};

    // Create jobs with different statuses
    jobMonitorService->onJobStatusChanged(jobIds[0], JobStatus::PENDING,
                                          JobStatus::RUNNING);
    jobMonitorService->onJobStatusChanged(jobIds[1], JobStatus::PENDING,
                                          JobStatus::RUNNING);
    jobMonitorService->onJobStatusChanged(jobIds[2], JobStatus::RUNNING,
                                          JobStatus::COMPLETED);

    // Test getAllActiveJobs
    auto activeJobs = jobMonitorService->getAllActiveJobs();
    assert(activeJobs.size() >= 2); // At least the two running jobs
    std::cout << "✓ getAllActiveJobs returned " << activeJobs.size()
              << " active jobs" << std::endl;

    // Test getJobsByStatus
    auto runningJobs = jobMonitorService->getJobsByStatus(JobStatus::RUNNING);
    assert(runningJobs.size() >= 2);
    std::cout << "✓ getJobsByStatus(RUNNING) returned " << runningJobs.size()
              << " jobs" << std::endl;

    auto completedJobs =
        jobMonitorService->getJobsByStatus(JobStatus::COMPLETED);
    assert(completedJobs.size() >= 1);
    std::cout << "✓ getJobsByStatus(COMPLETED) returned "
              << completedJobs.size() << " jobs" << std::endl;

    // Test individual job retrieval
    for (const auto &jobId : jobIds) {
      auto jobData = jobMonitorService->getJobMonitoringData(jobId);
      assert(jobData.jobId == jobId);
      std::cout << "✓ Successfully retrieved data for job: " << jobId
                << std::endl;
    }

    // Test retrieval of non-existent job
    auto nonExistentJob =
        jobMonitorService->getJobMonitoringData("non_existent_job");
    assert(nonExistentJob.jobId == "non_existent_job");
    std::cout << "✓ Non-existent job retrieval handled correctly" << std::endl;
  }

  /**
   * @brief Unit test verifying active-job tracking behavior in JobMonitorService.
   *
   * This test exercises the lifecycle of a single job ID to ensure the service
   * correctly tracks active jobs: initially not active, becomes active when the
   * status transitions to RUNNING, appears in the active job list and count,
   * and is removed from active tracking when the status transitions to COMPLETED.
   *
   * The test uses assertions to validate isJobActive, getActiveJobCount,
   * getActiveJobIds, and onJobStatusChanged behaviors for the tracked job ID.
   */
  void testActiveJobTracking() {
    std::cout << "\n--- Test: Active Job Tracking ---" << std::endl;

    std::string trackingJobId = "tracking_job_001";

    // Initially job should not be active
    assert(!jobMonitorService->isJobActive(trackingJobId));

    // Start job
    jobMonitorService->onJobStatusChanged(trackingJobId, JobStatus::PENDING,
                                          JobStatus::RUNNING);
    assert(jobMonitorService->isJobActive(trackingJobId));
    std::cout << "✓ Job correctly tracked as active" << std::endl;

    // Get active job count
    size_t activeCount = jobMonitorService->getActiveJobCount();
    std::cout << "✓ Active job count: " << activeCount << std::endl;

    // Get active job IDs
    auto activeJobIds = jobMonitorService->getActiveJobIds();
    bool found = std::find(activeJobIds.begin(), activeJobIds.end(),
                           trackingJobId) != activeJobIds.end();
    assert(found);
    std::cout << "✓ Job ID found in active jobs list" << std::endl;

    // Complete job
    jobMonitorService->onJobStatusChanged(trackingJobId, JobStatus::RUNNING,
                                          JobStatus::COMPLETED);
    assert(!jobMonitorService->isJobActive(trackingJobId));
    std::cout << "✓ Job correctly removed from active tracking after completion"
              << std::endl;
  }

  /**
   * @brief Exercises WebSocket broadcasting paths of the JobMonitorService.
   *
   * Starts the WebSocket manager and publishes a sequence of messages to verify
   * broadcasting behavior: a job status update, a progress update, a log
   * message, and job metrics. Intended for use in the test harness; it reports
   * progress to stdout but does not perform assertions itself.
   */
  void testWebSocketMessageBroadcasting() {
    std::cout << "\n--- Test: WebSocket Message Broadcasting ---" << std::endl;

    std::string broadcastJobId = "broadcast_job_001";

    // Start WebSocket manager
    wsManager->start();

    // Test job status update broadcasting
    JobStatusUpdate statusUpdate;
    statusUpdate.jobId = broadcastJobId;
    statusUpdate.status = JobStatus::RUNNING;
    statusUpdate.previousStatus = JobStatus::PENDING;
    statusUpdate.timestamp = std::chrono::system_clock::now();
    statusUpdate.progressPercent = 0;
    statusUpdate.currentStep = "Starting job";

    jobMonitorService->broadcastJobStatusUpdate(statusUpdate);
    std::cout << "✓ Job status update broadcasted successfully" << std::endl;

    // Test progress broadcasting
    jobMonitorService->broadcastJobProgress(broadcastJobId, 50,
                                            "Halfway complete");
    std::cout << "✓ Job progress broadcasted successfully" << std::endl;

    // Test log message broadcasting
    LogMessage logMsg;
    logMsg.jobId = broadcastJobId;
    logMsg.level = "INFO";
    logMsg.component = "JobMonitorService";
    logMsg.message = "Test log message";
    logMsg.timestamp = std::chrono::system_clock::now();

    jobMonitorService->broadcastLogMessage(logMsg);
    std::cout << "✓ Log message broadcasted successfully" << std::endl;

    // Test metrics broadcasting
    JobMetrics metrics;
    metrics.recordsProcessed = 1000;
    metrics.recordsSuccessful = 950;
    metrics.recordsFailed = 50;
    metrics.processingRate = 100.5;

    jobMonitorService->broadcastJobMetrics(broadcastJobId, metrics);
    std::cout << "✓ Job metrics broadcasted successfully" << std::endl;
  }

  void testJobMetricsHandling() {
    std::cout << "\n--- Test: Job Metrics Handling ---" << std::endl;

    std::string metricsJobId = "metrics_job_001";

    // Initialize job
    jobMonitorService->onJobStatusChanged(metricsJobId, JobStatus::PENDING,
                                          JobStatus::RUNNING);

    // Test metrics update
    JobMetrics testMetrics;
    testMetrics.recordsProcessed = 500;
    testMetrics.recordsSuccessful = 475;
    testMetrics.recordsFailed = 25;
    testMetrics.processingRate = 50.0;
    testMetrics.memoryUsage = 1024 * 1024; // 1MB
    testMetrics.cpuUsage = 75.5;
    testMetrics.executionTime = std::chrono::milliseconds(30000);

    jobMonitorService->updateJobMetrics(metricsJobId, testMetrics);

    // Retrieve and verify metrics
    auto retrievedMetrics = jobMonitorService->getJobMetrics(metricsJobId);
    assert(retrievedMetrics.recordsProcessed == 500);
    assert(retrievedMetrics.recordsSuccessful == 475);
    assert(retrievedMetrics.recordsFailed == 25);
    assert(retrievedMetrics.processingRate == 50.0);
    assert(retrievedMetrics.memoryUsage == 1024 * 1024);
    assert(retrievedMetrics.cpuUsage == 75.5);

    std::cout << "✓ Job metrics correctly updated and retrieved" << std::endl;

    // Test metrics for non-existent job
    auto emptyMetrics =
        jobMonitorService->getJobMetrics("non_existent_metrics_job");
    assert(emptyMetrics.recordsProcessed == 0);
    std::cout << "✓ Empty metrics returned for non-existent job" << std::endl;
  }

  /**
   * @brief Verifies integration between the JobMonitorService and the NotificationService.
   *
   * Runs a sequence of checks that exercise failure notifications and the runtime
   * enable/disable controls for notifications:
   * - Clears previously recorded alerts from the mock notification service.
   * - Triggers a job status transition to FAILED and asserts a failure alert is recorded.
   * - Disables notifications, triggers another failure, and asserts no new alert is recorded.
   * - Re-enables notifications.
   *
   * Side effects:
   * - Mutates the MockNotificationService's recorded alert vectors.
   * - Calls JobMonitorService APIs to change status and toggle notifications.
   * - Uses a short sleep to allow asynchronous notification processing.
   *
   * Assertions are used to validate expected outcomes; the function relies on a
   * mocked NotificationService and may abort the test run if assertions fail.
   */
  void testNotificationIntegration() {
    std::cout << "\n--- Test: Notification Integration ---" << std::endl;

    // Clear previous notifications
    auto mockService =
        std::static_pointer_cast<MockNotificationService>(notificationService);
    mockService->failureAlerts.clear();
    mockService->timeoutWarnings.clear();

    std::string notificationJobId = "notification_job_001";

    // Test job failure notification
    jobMonitorService->onJobStatusChanged(notificationJobId, JobStatus::RUNNING,
                                          JobStatus::FAILED);

    // Give some time for notification processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check if failure notification was sent
    assert(mockService->failureAlerts.size() >= 1);
    std::cout << "✓ Job failure notification sent successfully" << std::endl;

    // Test notification enable/disable
    jobMonitorService->enableNotifications(false);

    std::string disabledNotificationJobId = "disabled_notification_job_001";
    size_t previousAlertCount = mockService->failureAlerts.size();

    jobMonitorService->onJobStatusChanged(
        disabledNotificationJobId, JobStatus::RUNNING, JobStatus::FAILED);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should not have increased
    assert(mockService->failureAlerts.size() == previousAlertCount);
    std::cout << "✓ Notifications correctly disabled" << std::endl;

    // Re-enable notifications
    jobMonitorService->enableNotifications(true);
    std::cout << "✓ Notifications re-enabled" << std::endl;
  }

  /**
   * @brief Exercises configuration-related APIs of the JobMonitorService.
   *
   * Applies and verifies runtime configuration changes on the test instance of
   * JobMonitorService: adjusts the maximum number of recent logs retained,
   * updates the progress-update threshold, and toggles notifications on and off.
   *
   * This is a test helper that causes side effects on the member `jobMonitorService`.
   */
  void testConfigurationSettings() {
    std::cout << "\n--- Test: Configuration Settings ---" << std::endl;

    // Test max recent logs setting
    jobMonitorService->setMaxRecentLogs(100);
    std::cout << "✓ Max recent logs setting applied" << std::endl;

    // Test progress update threshold setting
    jobMonitorService->setProgressUpdateThreshold(15);
    std::cout << "✓ Progress update threshold setting applied" << std::endl;

    // Test notification enable/disable
    jobMonitorService->enableNotifications(true);
    jobMonitorService->enableNotifications(false);
    std::cout << "✓ Notification enable/disable settings applied" << std::endl;
  }

  /**
   * @brief Runs unit tests that verify graceful handling of error and edge cases.
   *
   * This test executes several error-condition scenarios against the JobMonitorService:
   * - Invokes status and progress callbacks on a service instance that has not been started.
   * - Calls the same callbacks with invalid (empty) job IDs.
   * - Supplies extreme progress values (below 0 and above 100).
   *
   * The test asserts that these operations do not crash the process and are handled
   * gracefully by the service (e.g., no uncaught exceptions, appropriate logging or no-ops).
   *
   * Output: prints concise pass messages for each sub-check to stdout.
   */
  void testErrorHandling() {
    std::cout << "\n--- Test: Error Handling ---" << std::endl;

    // Test operations when service is not running
    auto testService = std::make_shared<JobMonitorService>();

    // These should not crash but should log warnings
    testService->onJobStatusChanged("test_job", JobStatus::PENDING,
                                    JobStatus::RUNNING);
    testService->onJobProgressUpdated("test_job", 50, "Test step");

    std::cout << "✓ Operations on non-running service handled gracefully"
              << std::endl;

    // Test with invalid job IDs
    jobMonitorService->onJobStatusChanged("", JobStatus::PENDING,
                                          JobStatus::RUNNING);
    jobMonitorService->onJobProgressUpdated("", 50, "Test step");

    std::cout << "✓ Invalid job ID operations handled gracefully" << std::endl;

    // Test extreme values
    jobMonitorService->onJobProgressUpdated("extreme_test_job", -10,
                                            "Negative progress");
    jobMonitorService->onJobProgressUpdated("extreme_test_job", 150,
                                            "Over 100% progress");

    std::cout << "✓ Extreme progress values handled gracefully" << std::endl;
  }
};

/**
 * @brief Entry point that runs the Job Monitor Service test suite.
 *
 * Constructs the test harness, executes all tests via JobMonitorServiceTest::runAllTests(),
 * and reports the overall result. On success prints a confirmation and returns 0.
 * If a std::exception is thrown the exception message is printed and the process returns 1.
 * Any other uncaught exception also causes the process to return 1.
 *
 * @return int 0 on success; 1 on failure (exception thrown).
 */
int main() {
  try {
    JobMonitorServiceTest test;
    test.runAllTests();

    std::cout << "\n🎉 All Job Monitor Service tests passed!" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Test failed with unknown exception" << std::endl;
    return 1;
  }
}