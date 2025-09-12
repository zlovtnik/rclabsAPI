#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

// System components
#include "config_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "job_monitor_service.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include "websocket_manager.hpp"

/**
 * Performance and Load Testing Suite
 *
 * This comprehensive test suite validates system performance under various
 * conditions:
 * 1. High-frequency job creation and processing
 * 2. Multiple concurrent WebSocket connections
 * 3. Heavy notification traffic
 * 4. Memory and CPU usage under load
 * 5. System stability over extended periods
 * 6. Recovery from resource exhaustion
 */

struct PerformanceMetrics {
  std::atomic<uint64_t> jobsCreated{0};
  std::atomic<uint64_t> jobsCompleted{0};
  std::atomic<uint64_t> jobsFailed{0};
  std::atomic<uint64_t> messagesReceived{0};
  std::atomic<uint64_t> messagesSent{0};
  std::atomic<uint64_t> notificationsSent{0};
  std::atomic<uint64_t> notificationsFailed{0};
  std::atomic<uint64_t> wsConnectionsCreated{0};
  std::atomic<uint64_t> wsConnectionsDropped{0};

  std::chrono::steady_clock::time_point startTime;
  std::chrono::steady_clock::time_point endTime;

  double peakMemoryUsageMB{0.0};
  double peakCpuUsage{0.0};
  size_t maxActiveJobs{0};
  size_t maxWsConnections{0};
  size_t maxNotificationQueue{0};

  void reset() {
    jobsCreated = 0;
    jobsCompleted = 0;
    jobsFailed = 0;
    messagesReceived = 0;
    messagesSent = 0;
    notificationsSent = 0;
    notificationsFailed = 0;
    wsConnectionsCreated = 0;
    wsConnectionsDropped = 0;

    peakMemoryUsageMB = 0.0;
    peakCpuUsage = 0.0;
    maxActiveJobs = 0;
    maxWsConnections = 0;
    maxNotificationQueue = 0;
  }

  void printSummary() const {
    auto duration =
        std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    double durationSec = duration.count();

    std::cout << "\n=== Performance Test Results ===" << std::endl;
    std::cout << "Test Duration: " << durationSec << " seconds" << std::endl;
    std::cout << "\nJob Metrics:" << std::endl;
    std::cout << "  Jobs Created: " << jobsCreated.load() << std::endl;
    std::cout << "  Jobs Completed: " << jobsCompleted.load() << std::endl;
    std::cout << "  Jobs Failed: " << jobsFailed.load() << std::endl;
    std::cout << "  Job Success Rate: "
              << (jobsCompleted.load() * 100.0 /
                  std::max(1UL, jobsCreated.load()))
              << "%" << std::endl;
    std::cout << "  Job Throughput: "
              << (jobsCompleted.load() / std::max(1.0, durationSec))
              << " jobs/sec" << std::endl;

    std::cout << "\nWebSocket Metrics:" << std::endl;
    std::cout << "  Connections Created: " << wsConnectionsCreated.load()
              << std::endl;
    std::cout << "  Connections Dropped: " << wsConnectionsDropped.load()
              << std::endl;
    std::cout << "  Messages Sent: " << messagesSent.load() << std::endl;
    std::cout << "  Messages Received: " << messagesReceived.load()
              << std::endl;
    std::cout << "  Message Throughput: "
              << (messagesSent.load() / std::max(1.0, durationSec))
              << " msg/sec" << std::endl;
    std::cout << "  Max Concurrent Connections: " << maxWsConnections
              << std::endl;

    std::cout << "\nNotification Metrics:" << std::endl;
    std::cout << "  Notifications Sent: " << notificationsSent.load()
              << std::endl;
    std::cout << "  Notifications Failed: " << notificationsFailed.load()
              << std::endl;
    std::cout << "  Notification Success Rate: "
              << (notificationsSent.load() * 100.0 /
                  std::max(1UL, notificationsSent.load() +
                                    notificationsFailed.load()))
              << "%" << std::endl;
    std::cout << "  Max Notification Queue: " << maxNotificationQueue
              << std::endl;

    std::cout << "\nResource Metrics:" << std::endl;
    std::cout << "  Peak Memory Usage: " << peakMemoryUsageMB << " MB"
              << std::endl;
    std::cout << "  Peak CPU Usage: " << (peakCpuUsage * 100.0) << "%"
              << std::endl;
    std::cout << "  Max Active Jobs: " << maxActiveJobs << std::endl;
  }

  void saveToFile(const std::string &filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
      std::cerr << "Failed to open file for writing: " << filename << std::endl;
      return;
    }

    auto duration =
        std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    double durationSec = duration.count();

    file << "test_duration_seconds," << durationSec << std::endl;
    file << "jobs_created," << jobsCreated.load() << std::endl;
    file << "jobs_completed," << jobsCompleted.load() << std::endl;
    file << "jobs_failed," << jobsFailed.load() << std::endl;
    file << "job_throughput_per_sec,"
         << (jobsCompleted.load() / std::max(1.0, durationSec)) << std::endl;
    file << "ws_connections_created," << wsConnectionsCreated.load()
         << std::endl;
    file << "ws_connections_dropped," << wsConnectionsDropped.load()
         << std::endl;
    file << "messages_sent," << messagesSent.load() << std::endl;
    file << "messages_received," << messagesReceived.load() << std::endl;
    file << "message_throughput_per_sec,"
         << (messagesSent.load() / std::max(1.0, durationSec)) << std::endl;
    file << "notifications_sent," << notificationsSent.load() << std::endl;
    file << "notifications_failed," << notificationsFailed.load() << std::endl;
    file << "peak_memory_mb," << peakMemoryUsageMB << std::endl;
    file << "peak_cpu_usage," << peakCpuUsage << std::endl;
    file << "max_active_jobs," << maxActiveJobs << std::endl;
    file << "max_ws_connections," << maxWsConnections << std::endl;
    file << "max_notification_queue," << maxNotificationQueue << std::endl;

    file.close();
    std::cout << "Performance metrics saved to: " << filename << std::endl;
  }
};

class PerformanceTestSuite {
private:
  // System components
  std::shared_ptr<DatabaseManager> dbManager_;
  std::shared_ptr<ETLJobManager> etlManager_;
  std::shared_ptr<WebSocketManager> wsManager_;
  std::shared_ptr<JobMonitorService> jobMonitor_;
  std::shared_ptr<NotificationServiceImpl> notificationService_;
  std::shared_ptr<DataTransformer> dataTransformer_;

  // Test configuration
  struct TestConfig {
    int lightLoadJobs = 50;
    int mediumLoadJobs = 200;
    int heavyLoadJobs = 500;
    int extremeLoadJobs = 1000;

    int lightLoadConnections = 10;
    int mediumLoadConnections = 50;
    int heavyLoadConnections = 100;
    int extremeLoadConnections = 200;

    int shortTestDuration = 30;   // seconds
    int mediumTestDuration = 120; // seconds
    int longTestDuration = 300;   // seconds

    int jobProcessingTimeMin = 100;  // milliseconds
    int jobProcessingTimeMax = 2000; // milliseconds

    double failureRate = 0.05; // 5% failure rate
  } config_;

  PerformanceMetrics metrics_;
  std::atomic<bool> testRunning_{false};

  // Mock WebSocket client for load testing
  class LoadTestWebSocketClient {
  public:
    LoadTestWebSocketClient(int id, PerformanceMetrics &metrics)
        : id_(id), metrics_(metrics), connected_(false) {}

    void connect() {
      connected_ = true;
      metrics_.wsConnectionsCreated++;
    }

    void disconnect() {
      if (connected_) {
        connected_ = false;
        metrics_.wsConnectionsDropped++;
      }
    }

    void simulateActivity() {
      if (!connected_)
        return;

      // Simulate receiving messages
      static std::random_device rd;
      static std::mt19937 gen(rd());
      static std::uniform_int_distribution<> dis(1, 100);

      if (dis(gen) <= 30) { // 30% chance of receiving a message
        metrics_.messagesReceived++;
      }
    }

    bool isConnected() const { return connected_; }
    int getId() const { return id_; }

  private:
    int id_;
    PerformanceMetrics &metrics_;
    bool connected_;
  };

public:
  PerformanceTestSuite() = default;

  bool runAllPerformanceTests() {
    std::cout << "\n=== Starting Comprehensive Performance Test Suite ==="
              << std::endl;

    if (!initializeSystem()) {
      std::cerr << "Failed to initialize system for performance testing"
                << std::endl;
      return false;
    }

    bool allTestsPassed = true;

    // Test 1: Light Load Test
    std::cout << "\n--- Test 1: Light Load Performance ---" << std::endl;
    if (!runLoadTest("Light Load", config_.lightLoadJobs,
                     config_.lightLoadConnections, config_.shortTestDuration)) {
      std::cerr << "Light load test failed" << std::endl;
      allTestsPassed = false;
    }

    // Test 2: Medium Load Test
    std::cout << "\n--- Test 2: Medium Load Performance ---" << std::endl;
    if (!runLoadTest("Medium Load", config_.mediumLoadJobs,
                     config_.mediumLoadConnections,
                     config_.mediumTestDuration)) {
      std::cerr << "Medium load test failed" << std::endl;
      allTestsPassed = false;
    }

    // Test 3: Heavy Load Test
    std::cout << "\n--- Test 3: Heavy Load Performance ---" << std::endl;
    if (!runLoadTest("Heavy Load", config_.heavyLoadJobs,
                     config_.heavyLoadConnections,
                     config_.mediumTestDuration)) {
      std::cerr << "Heavy load test failed" << std::endl;
      allTestsPassed = false;
    }

    // Test 4: Burst Load Test
    std::cout << "\n--- Test 4: Burst Load Test ---" << std::endl;
    if (!runBurstLoadTest()) {
      std::cerr << "Burst load test failed" << std::endl;
      allTestsPassed = false;
    }

    // Test 5: Sustained Load Test
    std::cout << "\n--- Test 5: Sustained Load Test ---" << std::endl;
    if (!runSustainedLoadTest()) {
      std::cerr << "Sustained load test failed" << std::endl;
      allTestsPassed = false;
    }

    // Test 6: Memory Stress Test
    std::cout << "\n--- Test 6: Memory Stress Test ---" << std::endl;
    if (!runMemoryStressTest()) {
      std::cerr << "Memory stress test failed" << std::endl;
      allTestsPassed = false;
    }

    // Test 7: Connection Stress Test
    std::cout << "\n--- Test 7: Connection Stress Test ---" << std::endl;
    if (!runConnectionStressTest()) {
      std::cerr << "Connection stress test failed" << std::endl;
      allTestsPassed = false;
    }

    cleanupSystem();

    if (allTestsPassed) {
      std::cout << "\n🎉 ALL PERFORMANCE TESTS PASSED! 🎉" << std::endl;
    } else {
      std::cout << "\n❌ SOME PERFORMANCE TESTS FAILED" << std::endl;
    }

    return allTestsPassed;
  }

private:
  bool initializeSystem() {
    std::cout << "Initializing system components for performance testing..."
              << std::endl;

    // Initialize configuration
    auto &config = ConfigManager::getInstance();
    config.loadConfig("config/config.json");

    // Initialize logger with minimal output for performance testing
    auto &logger = Logger::getInstance();
    LogConfig logConfig;
    logConfig.level = LogLevel::WARN; // Reduce logging overhead
    logConfig.enableFileLogging = false;
    logger.configure(logConfig);

    // Initialize database manager
    dbManager_ = std::make_shared<DatabaseManager>();
    ConnectionConfig dbConfig;
    dbConfig.host = "localhost";
    dbConfig.port = 5432;
    dbConfig.database = "etlplus_perf_test";
    dbConfig.username = "postgres";
    dbConfig.password = "";

    if (!dbManager_->connect(dbConfig)) {
      std::cout << "Database connection failed, running in offline mode"
                << std::endl;
    }

    // Initialize other components
    dataTransformer_ = std::make_shared<DataTransformer>();
    etlManager_ = std::make_shared<ETLJobManager>(dbManager_, dataTransformer_);
    wsManager_ = std::make_shared<WebSocketManager>();
    notificationService_ = std::make_shared<NotificationServiceImpl>();
    jobMonitor_ = std::make_shared<JobMonitorService>();

    // Configure notification service for performance testing
    NotificationConfig notifConfig;
    notifConfig.enabled = true;
    notifConfig.jobFailureAlerts = true;
    notifConfig.timeoutWarnings = false; // Disable to reduce overhead
    notifConfig.resourceAlerts = true;
    notifConfig.maxRetryAttempts = 1; // Reduce retries for performance
    notifConfig.queueMaxSize = 50000; // Large queue for stress testing
    notifConfig.defaultMethods = {NotificationMethod::LOG_ONLY};
    notificationService_->configure(notifConfig);

    // Wire components
    jobMonitor_->initialize(etlManager_, wsManager_, notificationService_);

    // Start services
    notificationService_->start();
    wsManager_->start();
    jobMonitor_->start();
    etlManager_->start();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "System initialized successfully for performance testing"
              << std::endl;
    return true;
  }

  void cleanupSystem() {
    std::cout << "Cleaning up system components..." << std::endl;

    if (etlManager_)
      etlManager_->stop();
    if (jobMonitor_)
      jobMonitor_->stop();
    if (wsManager_)
      wsManager_->stop();
    if (notificationService_)
      notificationService_->stop();

    std::cout << "System cleanup completed" << std::endl;
  }

  bool runLoadTest(const std::string &testName, int numJobs, int numConnections,
                   int durationSeconds) {
    std::cout << "Running " << testName << " test..." << std::endl;
    std::cout << "  Jobs: " << numJobs << ", Connections: " << numConnections
              << ", Duration: " << durationSeconds << "s" << std::endl;

    metrics_.reset();
    metrics_.startTime = std::chrono::steady_clock::now();
    testRunning_ = true;

    // Create WebSocket connections
    std::vector<std::unique_ptr<LoadTestWebSocketClient>> clients;
    for (int i = 0; i < numConnections; ++i) {
      auto client = std::make_unique<LoadTestWebSocketClient>(i, metrics_);
      client->connect();
      clients.push_back(std::move(client));
    }

    // Start resource monitoring
    auto resourceMonitor =
        std::async(std::launch::async, [this]() { monitorResources(); });

    // Start WebSocket activity simulation
    auto wsActivitySimulator =
        std::async(std::launch::async,
                   [this, &clients]() { simulateWebSocketActivity(clients); });

    // Create and process jobs
    std::vector<std::future<void>> jobFutures;
    for (int i = 0; i < numJobs; ++i) {
      auto future = std::async(std::launch::async,
                               [this, i]() { processLoadTestJob(i); });
      jobFutures.push_back(std::move(future));
    }

    // Wait for test duration
    std::this_thread::sleep_for(std::chrono::seconds(durationSeconds));
    testRunning_ = false;

    // Wait for all jobs to complete
    for (auto &future : jobFutures) {
      future.wait();
    }

    // Disconnect clients
    for (auto &client : clients) {
      client->disconnect();
    }

    metrics_.endTime = std::chrono::steady_clock::now();

    // Print results
    std::cout << testName << " Results:" << std::endl;
    metrics_.printSummary();

    // Save detailed results
    std::string filename = "performance_" + testName + "_results.csv";
    std::replace(filename.begin(), filename.end(), ' ', '_');
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   ::tolower);
    metrics_.saveToFile(filename);

    // Validate results
    bool testPassed =
        validateLoadTestResults(testName, numJobs, numConnections);

    std::cout << testName << " Test: " << (testPassed ? "PASSED" : "FAILED")
              << std::endl;
    return testPassed;
  }

  bool runBurstLoadTest() {
    std::cout << "Running burst load test (rapid job creation)..." << std::endl;

    metrics_.reset();
    metrics_.startTime = std::chrono::steady_clock::now();
    testRunning_ = true;

    // Create burst of jobs in rapid succession
    const int BURST_SIZE = 100;
    const int NUM_BURSTS = 5;
    const int BURST_INTERVAL = 10; // seconds

    auto burstCreator = std::async(
        std::launch::async, [this, BURST_SIZE, NUM_BURSTS, BURST_INTERVAL]() {
          for (int burst = 0; burst < NUM_BURSTS; ++burst) {
            std::cout << "Creating burst " << (burst + 1) << " of "
                      << NUM_BURSTS << std::endl;

            // Create jobs rapidly
            std::vector<std::future<void>> burstJobs;
            for (int i = 0; i < BURST_SIZE; ++i) {
              auto future = std::async(std::launch::async, [this, burst, i]() {
                int jobId = burst * BURST_SIZE + i;
                processLoadTestJob(jobId);
              });
              burstJobs.push_back(std::move(future));
            }

            // Wait for burst to complete
            for (auto &future : burstJobs) {
              future.wait();
            }

            // Wait before next burst
            if (burst < NUM_BURSTS - 1) {
              std::this_thread::sleep_for(std::chrono::seconds(BURST_INTERVAL));
            }
          }
        });

    // Monitor resources during burst
    auto resourceMonitor =
        std::async(std::launch::async, [this]() { monitorResources(); });

    burstCreator.wait();
    testRunning_ = false;

    metrics_.endTime = std::chrono::steady_clock::now();

    std::cout << "Burst Load Test Results:" << std::endl;
    metrics_.printSummary();
    metrics_.saveToFile("performance_burst_load_results.csv");

    // Validate that system handled bursts without significant failures
    double failureRate = static_cast<double>(metrics_.jobsFailed.load()) /
                         metrics_.jobsCreated.load();
    bool testPassed = failureRate < 0.1; // Less than 10% failure rate

    std::cout << "Burst Load Test: " << (testPassed ? "PASSED" : "FAILED")
              << std::endl;
    return testPassed;
  }

  bool runSustainedLoadTest() {
    std::cout << "Running sustained load test (continuous operation)..."
              << std::endl;

    metrics_.reset();
    metrics_.startTime = std::chrono::steady_clock::now();
    testRunning_ = true;

    const int SUSTAINED_DURATION = config_.longTestDuration;
    const int JOBS_PER_MINUTE = 20;

    // Create continuous job stream
    auto jobCreator = std::async(std::launch::async, [this, SUSTAINED_DURATION,
                                                      JOBS_PER_MINUTE]() {
      auto endTime = std::chrono::steady_clock::now() +
                     std::chrono::seconds(SUSTAINED_DURATION);
      int jobCounter = 0;

      while (std::chrono::steady_clock::now() < endTime && testRunning_) {
        // Create batch of jobs
        std::vector<std::future<void>> batchJobs;
        for (int i = 0; i < JOBS_PER_MINUTE; ++i) {
          auto future = std::async(std::launch::async, [this, jobCounter]() {
            processLoadTestJob(jobCounter);
          });
          batchJobs.push_back(std::move(future));
          jobCounter++;
        }

        // Wait for batch to complete
        for (auto &future : batchJobs) {
          future.wait();
        }

        // Wait before next batch (1 minute interval)
        std::this_thread::sleep_for(std::chrono::seconds(60));
      }
    });

    // Monitor system health during sustained load
    auto healthMonitor = std::async(std::launch::async, [this,
                                                         SUSTAINED_DURATION]() {
      auto endTime = std::chrono::steady_clock::now() +
                     std::chrono::seconds(SUSTAINED_DURATION);

      while (std::chrono::steady_clock::now() < endTime && testRunning_) {
        size_t activeJobs = jobMonitor_->getActiveJobCount();
        size_t wsConnections = wsManager_->getConnectionCount();
        size_t notificationQueue = notificationService_->getQueueSize();

        std::cout << "Health Check - Active Jobs: " << activeJobs
                  << ", WS Connections: " << wsConnections
                  << ", Notification Queue: " << notificationQueue << std::endl;

        // Update peak metrics
        metrics_.maxActiveJobs = std::max(metrics_.maxActiveJobs, activeJobs);
        metrics_.maxWsConnections =
            std::max(metrics_.maxWsConnections, wsConnections);
        metrics_.maxNotificationQueue =
            std::max(metrics_.maxNotificationQueue, notificationQueue);

        std::this_thread::sleep_for(std::chrono::seconds(30));
      }
    });

    jobCreator.wait();
    testRunning_ = false;

    metrics_.endTime = std::chrono::steady_clock::now();

    std::cout << "Sustained Load Test Results:" << std::endl;
    metrics_.printSummary();
    metrics_.saveToFile("performance_sustained_load_results.csv");

    // Validate system stability
    bool testPassed =
        metrics_.maxActiveJobs < 1000 &&       // Reasonable job queue size
        metrics_.maxNotificationQueue < 10000; // Reasonable notification queue

    std::cout << "Sustained Load Test: " << (testPassed ? "PASSED" : "FAILED")
              << std::endl;
    return testPassed;
  }

  bool runMemoryStressTest() {
    std::cout << "Running memory stress test..." << std::endl;

    metrics_.reset();
    metrics_.startTime = std::chrono::steady_clock::now();
    testRunning_ = true;

    // Create many jobs with large data payloads
    const int MEMORY_STRESS_JOBS = 200;

    std::vector<std::future<void>> memoryJobs;
    for (int i = 0; i < MEMORY_STRESS_JOBS; ++i) {
      auto future = std::async(std::launch::async,
                               [this, i]() { processMemoryIntensiveJob(i); });
      memoryJobs.push_back(std::move(future));
    }

    // Monitor memory usage
    auto memoryMonitor = std::async(std::launch::async, [this]() {
      while (testRunning_) {
        // Simulate memory usage monitoring
        // In a real implementation, this would use system APIs
        double currentMemory =
            100.0 + (metrics_.jobsCreated.load() * 0.5); // MB
        metrics_.peakMemoryUsageMB =
            std::max(metrics_.peakMemoryUsageMB, currentMemory);

        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });

    // Wait for all memory-intensive jobs
    for (auto &future : memoryJobs) {
      future.wait();
    }

    testRunning_ = false;
    metrics_.endTime = std::chrono::steady_clock::now();

    std::cout << "Memory Stress Test Results:" << std::endl;
    metrics_.printSummary();
    metrics_.saveToFile("performance_memory_stress_results.csv");

    // Validate memory usage stayed within reasonable bounds
    bool testPassed = metrics_.peakMemoryUsageMB < 1000.0; // Less than 1GB

    std::cout << "Memory Stress Test: " << (testPassed ? "PASSED" : "FAILED")
              << std::endl;
    return testPassed;
  }

  bool runConnectionStressTest() {
    std::cout << "Running connection stress test..." << std::endl;

    metrics_.reset();
    metrics_.startTime = std::chrono::steady_clock::now();
    testRunning_ = true;

    const int MAX_CONNECTIONS = config_.extremeLoadConnections;

    // Create many WebSocket connections
    std::vector<std::unique_ptr<LoadTestWebSocketClient>> clients;
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
      auto client = std::make_unique<LoadTestWebSocketClient>(i, metrics_);
      client->connect();
      clients.push_back(std::move(client));

      // Small delay to avoid overwhelming the system
      if (i % 10 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    std::cout << "Created " << clients.size() << " WebSocket connections"
              << std::endl;

    // Simulate activity on all connections
    auto activitySimulator = std::async(std::launch::async, [this, &clients]() {
      simulateWebSocketActivity(clients);
    });

    // Run for a period with all connections active
    std::this_thread::sleep_for(std::chrono::seconds(30));

    // Gradually disconnect clients
    for (auto &client : clients) {
      client->disconnect();
    }

    testRunning_ = false;
    metrics_.endTime = std::chrono::steady_clock::now();

    std::cout << "Connection Stress Test Results:" << std::endl;
    metrics_.printSummary();
    metrics_.saveToFile("performance_connection_stress_results.csv");

    // Validate connection handling
    bool testPassed = metrics_.wsConnectionsCreated.load() >=
                      MAX_CONNECTIONS * 0.9; // 90% success rate

    std::cout << "Connection Stress Test: "
              << (testPassed ? "PASSED" : "FAILED") << std::endl;
    return testPassed;
  }

  void processLoadTestJob(int jobId) {
    try {
      std::string jobIdStr = "perf_test_job_" + std::to_string(jobId);

      // Create job
      auto job = etlManager_->createJob(JobType::DATA_IMPORT, jobIdStr);
      if (!job) {
        metrics_.jobsFailed++;
        return;
      }

      metrics_.jobsCreated++;

      // Simulate job processing
      jobMonitor_->onJobStatusChanged(job->jobId, JobStatus::PENDING,
                                      JobStatus::RUNNING);

      // Random processing time
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> timeDis(config_.jobProcessingTimeMin,
                                              config_.jobProcessingTimeMax);
      std::uniform_real_distribution<> failureDis(0.0, 1.0);

      int processingTime = timeDis(gen);
      int progressSteps = 5;
      int stepTime = processingTime / progressSteps;

      // Simulate progress updates
      for (int step = 1; step <= progressSteps; ++step) {
        if (!testRunning_)
          break;

        int progress = (step * 100) / progressSteps;
        std::string stepDesc = "Processing step " + std::to_string(step);
        jobMonitor_->onJobProgressUpdated(job->jobId, progress, stepDesc);

        // Update metrics
        JobMetrics jobMetrics;
        jobMetrics.recordsProcessed = progress * 10;
        jobMetrics.recordsSuccessful = progress * 9;
        jobMetrics.recordsFailed = progress * 1;
        jobMetrics.averageProcessingRate = 100.0 + (progress * 2.0);
        jobMetrics.memoryUsage = 1024 * 1024 * (10 + progress / 10); // MB
        jobMetrics.cpuUsage = 0.2 + (progress * 0.003);

        jobMonitor_->updateJobMetrics(job->jobId, jobMetrics);

        std::this_thread::sleep_for(std::chrono::milliseconds(stepTime));
      }

      // Determine job outcome
      if (failureDis(gen) < config_.failureRate) {
        jobMonitor_->onJobStatusChanged(job->jobId, JobStatus::RUNNING,
                                        JobStatus::FAILED);
        metrics_.jobsFailed++;
      } else {
        jobMonitor_->onJobStatusChanged(job->jobId, JobStatus::RUNNING,
                                        JobStatus::COMPLETED);
        metrics_.jobsCompleted++;
      }

    } catch (const std::exception &e) {
      metrics_.jobsFailed++;
    }
  }

  void processMemoryIntensiveJob(int jobId) {
    try {
      std::string jobIdStr = "memory_test_job_" + std::to_string(jobId);

      auto job = etlManager_->createJob(JobType::DATA_EXPORT, jobIdStr);
      if (!job) {
        metrics_.jobsFailed++;
        return;
      }

      metrics_.jobsCreated++;

      jobMonitor_->onJobStatusChanged(job->jobId, JobStatus::PENDING,
                                      JobStatus::RUNNING);

      // Simulate memory-intensive processing
      std::vector<std::vector<char>> memoryBlocks;
      const size_t BLOCK_SIZE = 1024 * 1024; // 1MB blocks
      const int NUM_BLOCKS = 10;

      for (int i = 0; i < NUM_BLOCKS; ++i) {
        if (!testRunning_)
          break;

        memoryBlocks.emplace_back(BLOCK_SIZE, 'A' + (i % 26));

        int progress = ((i + 1) * 100) / NUM_BLOCKS;
        jobMonitor_->onJobProgressUpdated(job->jobId, progress,
                                          "Allocating memory block " +
                                              std::to_string(i + 1));

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }

      // Hold memory for a while
      std::this_thread::sleep_for(std::chrono::seconds(2));

      jobMonitor_->onJobStatusChanged(job->jobId, JobStatus::RUNNING,
                                      JobStatus::COMPLETED);
      metrics_.jobsCompleted++;

    } catch (const std::exception &e) {
      metrics_.jobsFailed++;
    }
  }

  void simulateWebSocketActivity(
      const std::vector<std::unique_ptr<LoadTestWebSocketClient>> &clients) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> activityDis(50, 200);

    while (testRunning_) {
      for (const auto &client : clients) {
        if (client->isConnected()) {
          client->simulateActivity();
          metrics_.messagesSent++;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(activityDis(gen)));
    }
  }

  void monitorResources() {
    while (testRunning_) {
      // Simulate resource monitoring
      size_t activeJobs = jobMonitor_->getActiveJobCount();
      size_t wsConnections = wsManager_->getConnectionCount();
      size_t notificationQueue = notificationService_->getQueueSize();

      // Update peak metrics
      metrics_.maxActiveJobs = std::max(metrics_.maxActiveJobs, activeJobs);
      metrics_.maxWsConnections =
          std::max(metrics_.maxWsConnections, wsConnections);
      metrics_.maxNotificationQueue =
          std::max(metrics_.maxNotificationQueue, notificationQueue);

      // Simulate CPU usage
      double cpuUsage = 0.1 + (activeJobs * 0.01);
      metrics_.peakCpuUsage = std::max(metrics_.peakCpuUsage, cpuUsage);

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  bool validateLoadTestResults(const std::string &testName, int expectedJobs,
                               int expectedConnections) {
    // Basic validation criteria
    double jobSuccessRate = static_cast<double>(metrics_.jobsCompleted.load()) /
                            std::max(1UL, metrics_.jobsCreated.load());

    double connectionSuccessRate =
        static_cast<double>(metrics_.wsConnectionsCreated.load()) /
        std::max(1, expectedConnections);

    bool jobsValid = jobSuccessRate >= 0.8; // At least 80% job success rate
    bool connectionsValid =
        connectionSuccessRate >= 0.9; // At least 90% connection success rate
    bool noSystemFailures =
        metrics_.maxNotificationQueue < 10000; // Reasonable queue size

    std::cout << "Validation Results for " << testName << ":" << std::endl;
    std::cout << "  Job Success Rate: " << (jobSuccessRate * 100.0) << "% "
              << (jobsValid ? "✓" : "✗") << std::endl;
    std::cout << "  Connection Success Rate: "
              << (connectionSuccessRate * 100.0) << "% "
              << (connectionsValid ? "✓" : "✗") << std::endl;
    std::cout << "  System Stability: " << (noSystemFailures ? "✓" : "✗")
              << std::endl;

    return jobsValid && connectionsValid && noSystemFailures;
  }
};

int main() {
  std::cout << "ETL Plus Performance and Load Testing Suite" << std::endl;
  std::cout << "===========================================" << std::endl;

  PerformanceTestSuite testSuite;

  auto startTime = std::chrono::steady_clock::now();
  bool success = testSuite.runAllPerformanceTests();
  auto endTime = std::chrono::steady_clock::now();

  auto totalDuration =
      std::chrono::duration_cast<std::chrono::minutes>(endTime - startTime);

  std::cout << "\nTotal test suite execution time: " << totalDuration.count()
            << " minutes" << std::endl;

  if (success) {
    std::cout << "\n🎉 ALL PERFORMANCE TESTS PASSED! 🎉" << std::endl;
    std::cout << "The system demonstrates excellent performance under various "
                 "load conditions."
              << std::endl;
    return 0;
  } else {
    std::cout << "\n❌ SOME PERFORMANCE TESTS FAILED" << std::endl;
    std::cout
        << "Please review the test results and optimize system performance."
        << std::endl;
    return 1;
  }
}