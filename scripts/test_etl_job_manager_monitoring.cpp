#include "../include/data_transformer.hpp"
#include "../include/database_manager.hpp"
#include "../include/etl_job_manager.hpp"
#include "../include/logger.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// JobMonitorServiceInterface definition for testing
class JobMonitorServiceInterface {
public:
  /**
 * @brief Virtual default destructor for the interface.
 *
 * Ensures derived implementations are correctly destroyed when deleted
 * through a pointer to JobMonitorServiceInterface.
 */
virtual ~JobMonitorServiceInterface() = default;
  virtual void onJobStatusChanged(const std::string &jobId, JobStatus oldStatus,
                                  JobStatus newStatus) = 0;
  virtual void onJobProgressUpdated(const std::string &jobId,
                                    int progressPercent,
                                    const std::string &currentStep) = 0;
};

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

  /**
 * @brief Default virtual destructor.
 *
 * Ensures derived mock implementations are properly destroyed through base
 * pointers. No additional cleanup is performed by the base class.
 */
virtual ~MockJobMonitorService() = default;

  /**
   * @brief Record a job status transition in the mock monitor.
   *
   * Creates a StatusChangeEvent containing the job identifier, the previous
   * and new JobStatus values, and a timestamp (std::chrono::system_clock::now()),
   * then appends it to the mock's internal statusChanges list.
   *
   * @param oldStatus The previous job status (state before the change).
   * @param newStatus The new job status (state after the change).
   */
  virtual void onJobStatusChanged(const std::string &jobId, JobStatus oldStatus,
                                  JobStatus newStatus) {
    StatusChangeEvent event;
    event.jobId = jobId;
    event.oldStatus = oldStatus;
    event.newStatus = newStatus;
    event.timestamp = std::chrono::system_clock::now();
    statusChanges.push_back(event);

    std::cout << "Mock Monitor: Job " << jobId << " status changed from "
              << static_cast<int>(oldStatus) << " to "
              << static_cast<int>(newStatus) << std::endl;
  }

  /**
   * @brief Record a job progress update in the mock monitor.
   *
   * Appends a ProgressUpdateEvent (including the current system timestamp) to
   * the mock's progressUpdates storage for the specified job. The event carries
   * the reported completion percentage and a short description of the current
   * processing step.
   *
   * @param jobId Identifier of the job being updated.
   * @param progressPercent Completion percentage (expected 0–100).
   * @param currentStep Short description of the current processing step.
   */
  virtual void onJobProgressUpdated(const std::string &jobId,
                                    int progressPercent,
                                    const std::string &currentStep) {
    ProgressUpdateEvent event;
    event.jobId = jobId;
    event.progressPercent = progressPercent;
    event.currentStep = currentStep;
    event.timestamp = std::chrono::system_clock::now();
    progressUpdates.push_back(event);

    std::cout << "Mock Monitor: Job " << jobId
              << " progress: " << progressPercent << "% - " << currentStep
              << std::endl;
  }

  /**
   * @brief Clear all recorded status change and progress update events.
   *
   * Removes all entries from the mock's internal event stores, resetting its
   * observed state so subsequent assertions start from an empty history.
   */
  void reset() {
    statusChanges.clear();
    progressUpdates.clear();
  }

  /**
   * @brief Checks whether a recorded status-change event matches a specific transition for a job.
   *
   * Searches the captured statusChanges for an event with the given job ID whose previous
   * status equals `from` and whose new status equals `to`.
   *
   * @param jobId Identifier of the job to check.
   * @param from Expected previous JobStatus.
   * @param to Expected new JobStatus.
   * @return true if a matching status-change event exists; false otherwise.
   */
  bool hasStatusChange(const std::string &jobId, JobStatus from,
                       JobStatus to) const {
    for (const auto &event : statusChanges) {
      if (event.jobId == jobId && event.oldStatus == from &&
          event.newStatus == to) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Checks whether a specific progress update was recorded for a job.
   *
   * Searches the stored progress update events for an entry that exactly matches
   * the given job ID, progress percentage, and step string.
   *
   * @param jobId Identifier of the job to look up.
   * @param progress Progress percentage to match (exact integer equality).
   * @param step Exact step description to match (string equality, case-sensitive).
   * @return true if a matching progress update exists; false otherwise.
   */
  bool hasProgressUpdate(const std::string &jobId, int progress,
                         const std::string &step) const {
    for (const auto &event : progressUpdates) {
      if (event.jobId == jobId && event.progressPercent == progress &&
          event.currentStep == step) {
        return true;
      }
    }
    return false;
  }

  /**
 * @brief Returns the number of recorded job status change events.
 *
 * @return size_t Count of StatusChangeEvent entries in the mock.
 */
size_t getStatusChangeCount() const { return statusChanges.size(); }
  /**
 * @brief Get the number of recorded progress update events.
 *
 * @return size_t Number of progress updates stored in the mock.
 */
size_t getProgressUpdateCount() const { return progressUpdates.size(); }
};

// Adapter to make MockJobMonitorService compatible with the interface
class JobMonitorService {
public:
  /**
 * @brief Virtual destructor for JobMonitorService.
 *
 * Ensures proper polymorphic destruction of derived monitor service implementations.
 */
virtual ~JobMonitorService() = default;
  virtual void onJobStatusChanged(const std::string &jobId, JobStatus oldStatus,
                                  JobStatus newStatus) = 0;
  virtual void onJobProgressUpdated(const std::string &jobId,
                                    int progressPercent,
                                    const std::string &currentStep) = 0;
};

class MockJobMonitorServiceAdapter : public JobMonitorServiceInterface {
private:
  std::shared_ptr<MockJobMonitorService> mock_;

public:
  /**
       * @brief Constructs an adapter that delegates monitoring callbacks to the given mock service.
       *
       * The adapter retains a shared reference to the provided MockJobMonitorService and forwards
       * onJobStatusChanged and onJobProgressUpdated calls to it.
       */
      MockJobMonitorServiceAdapter(std::shared_ptr<MockJobMonitorService> mock)
      : mock_(mock) {}

  /**
   * @brief Forwarding adapter that notifies the underlying mock of a job status change.
   *
   * Delegates the status-change callback to the wrapped MockJobMonitorService instance.
   *
   * @param jobId Identifier of the job whose status changed.
   * @param oldStatus Previous job status.
   * @param newStatus New job status.
   */
  void onJobStatusChanged(const std::string &jobId, JobStatus oldStatus,
                          JobStatus newStatus) override {
    mock_->onJobStatusChanged(jobId, oldStatus, newStatus);
  }

  /**
   * @brief Forwards a job progress update to the underlying mock monitor.
   *
   * This override delegates the progress notification to the wrapped MockJobMonitorService instance.
   *
   * @param jobId Identifier of the job whose progress changed.
   * @param progressPercent Progress percentage (0-100).
   * @param currentStep Short description of the current processing step.
   */
  void onJobProgressUpdated(const std::string &jobId, int progressPercent,
                            const std::string &currentStep) override {
    mock_->onJobProgressUpdated(jobId, progressPercent, currentStep);
  }
};

/**
 * @brief Tests that job status updates are published to an attached monitor.
 *
 * Schedules a test job with the ETLJobManager, attaches a MockJobMonitorService
 * (via its adapter), publishes RUNNING and COMPLETED status updates for the
 * job, and asserts that the mock recorded the expected status transitions
 * (PENDING→RUNNING and RUNNING→COMPLETED) and at least two status-change events.
 */
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
  assert(mockMonitor->hasStatusChange(testJobId, JobStatus::PENDING,
                                      JobStatus::RUNNING));
  assert(mockMonitor->hasStatusChange(testJobId, JobStatus::RUNNING,
                                      JobStatus::COMPLETED));

  std::cout << "✓ Job status event publishing test passed" << std::endl;
}

/**
 * @brief Tests that ETLJobManager publishes job progress events to an attached monitor.
 *
 * Creates a MockJobMonitorService and adapter, attaches it to an ETLJobManager, publishes
 * a sequence of progress updates (0, 25, 50, 75, 100) for a test job, and asserts that
 * the mock captured all five progress events with the expected progress values and step
 * descriptions.
 *
 * @note This is a unit test: it uses assertions to validate behavior and will abort on failure.
 */
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

/**
 * @brief Runs an integration-style test that verifies ETL job execution emits monitoring events.
 *
 * This test configures a mock JobMonitorService, connects a test database, starts an ETLJobManager,
 * schedules both an EXTRACT job and a FULL_ETL job, and asserts that the mock monitor receives the
 * expected status transitions and progress updates for each job. The manager is stopped at the end
 * of the test.
 *
 * Side effects:
 * - Opens a database connection via DatabaseManager.
 * - Starts and stops an ETLJobManager.
 * - Schedules and executes asynchronous jobs which generate monitor callbacks.
 */
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
  assert(mockMonitor->hasStatusChange(extractJobId, JobStatus::PENDING,
                                      JobStatus::RUNNING));
  assert(mockMonitor->hasStatusChange(extractJobId, JobStatus::RUNNING,
                                      JobStatus::COMPLETED));

  // Verify progress updates were sent
  assert(mockMonitor->hasProgressUpdate(extractJobId, 0,
                                        "Starting data extraction"));
  assert(mockMonitor->hasProgressUpdate(extractJobId, 100,
                                        "Data extraction completed"));

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
  assert(mockMonitor->hasProgressUpdate(fullETLJobId, 0,
                                        "Starting full ETL pipeline"));
  assert(mockMonitor->hasProgressUpdate(fullETLJobId, 10,
                                        "Extracting data from source"));
  assert(mockMonitor->hasProgressUpdate(fullETLJobId, 50,
                                        "Transforming extracted data"));
  assert(mockMonitor->hasProgressUpdate(fullETLJobId, 80,
                                        "Loading transformed data"));
  assert(mockMonitor->hasProgressUpdate(fullETLJobId, 100,
                                        "Full ETL pipeline completed"));

  // Verify status transitions
  assert(mockMonitor->hasStatusChange(fullETLJobId, JobStatus::PENDING,
                                      JobStatus::RUNNING));
  assert(mockMonitor->hasStatusChange(fullETLJobId, JobStatus::RUNNING,
                                      JobStatus::COMPLETED));

  jobManager.stop();

  std::cout << "✓ Job execution with monitoring test passed" << std::endl;
}

/**
 * @brief Runs an integration-style test verifying ETL job execution when no monitoring service is attached.
 *
 * This test sets up a DatabaseManager and DataTransformer, connects to a test database,
 * starts an ETLJobManager without attaching any JobMonitorService, schedules a simple
 * EXTRACT job, waits for completion, and asserts that the job exists and reached
 * JobStatus::COMPLETED. The test starts and stops the ETLJobManager as part of its lifecycle.
 *
 * Note: the test uses assertions for verification and will terminate the process if a check fails.
 */
void testJobExecutionWithoutMonitoring() {
  std::cout << "\n=== Testing Job Execution without Monitoring ==="
            << std::endl;

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

/**
 * @brief Tests integration between ETLJobManager and a JobMonitorService adapter.
 *
 * Schedules a TRANSFORM job on a manager that has been attached to a MockJobMonitorService
 * (via MockJobMonitorServiceAdapter), then publishes a RUNNING status, a 50% progress
 * update with the step message "Halfway through transformation", and a COMPLETED status.
 * Asserts that the mock monitor observed at least two status changes and at least one
 * progress update.
 *
 * This function has side effects: it mutates the provided ETLJobManager by setting its
 * monitor service, scheduling a job, and publishing status/progress events. It uses
 * assertions to validate that events were received by the mock monitor.
 */
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
  jobManager.publishJobProgress(testJobId, 50,
                                "Halfway through transformation");
  jobManager.publishJobStatusUpdate(testJobId, JobStatus::COMPLETED);

  // Verify events were received
  assert(mockMonitor->getStatusChangeCount() >= 2);
  assert(mockMonitor->getProgressUpdateCount() >= 1);

  std::cout << "✓ Monitor service integration test passed" << std::endl;
}

/**
 * @brief Test runner for the ETL Job Manager monitoring subsystem.
 *
 * Configures the test logger, executes the suite of monitoring tests
 * (status publishing, progress publishing, execution with/without monitoring,
 * and monitor integration), and reports overall success or failure.
 *
 * On success prints a celebratory message and returns 0. If any test throws
 * an exception the function prints an error message to stderr and returns 1.
 *
 * @return int Exit code: 0 if all tests pass, 1 on any exception.
 */
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

    std::cout << "\n🎉 All ETL Job Manager monitoring tests passed!"
              << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "❌ Test failed with unknown exception" << std::endl;
    return 1;
  }
}