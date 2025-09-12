#include "etl_job_manager.hpp"
#include "job_monitoring_models.hpp"
#include "system_metrics.hpp"
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>

using namespace ETLPlus::Metrics;
using namespace ::testing;

class SystemMetricsTest : public ::testing::Test {
protected:
  /**
   * @brief Test fixture setup: create a fresh SystemMetrics instance.
   *
   * Called before each test to allocate and initialize a new SystemMetrics
   * object stored in the fixture's `metrics` member.
   */
  void SetUp() override { metrics = std::make_unique<SystemMetrics>(); }

  /**
   * @brief Test fixture teardown: stop system metrics monitoring if active.
   *
   * Ensures that any active SystemMetrics monitoring started during a test is
   * stopped before the next test runs. Safe to call whether monitoring is
   * active or not.
   */
  void TearDown() override {
    if (metrics && metrics->isMonitoring()) {
      metrics->stopMonitoring();
    }
  }

  std::unique_ptr<SystemMetrics> metrics;
};

class JobMetricsCollectorTest : public ::testing::Test {
protected:
  /**
   * @brief Test fixture setup: create a JobMetricsCollector for tests.
   *
   * Constructs a JobMetricsCollector with job id "test_job_123" and stores it
   * in the fixture's `collector` unique_ptr for use by individual test cases.
   */
  void SetUp() override {
    collector = std::make_unique<JobMetricsCollector>("test_job_123");
  }

  /**
   * @brief Test fixture teardown that stops active job metrics collection.
   *
   * If a JobMetricsCollector instance exists and is currently collecting, this
   * stops collection to ensure a clean test teardown and release of any
   * background resources.
   */
  void TearDown() override {
    if (collector && collector->isCollecting()) {
      collector->stopCollection();
    }
  }

  std::unique_ptr<JobMetricsCollector> collector;
};

// SystemMetrics Tests

TEST_F(SystemMetricsTest, StartStopMonitoring) {
  EXPECT_FALSE(metrics->isMonitoring());

  metrics->startMonitoring();
  EXPECT_TRUE(metrics->isMonitoring());

  metrics->stopMonitoring();
  EXPECT_FALSE(metrics->isMonitoring());
}

TEST_F(SystemMetricsTest, DoubleStartStop) {
  // Starting twice should not cause issues
  metrics->startMonitoring();
  metrics->startMonitoring();
  EXPECT_TRUE(metrics->isMonitoring());

  // Stopping twice should not cause issues
  metrics->stopMonitoring();
  metrics->stopMonitoring();
  EXPECT_FALSE(metrics->isMonitoring());
}

TEST_F(SystemMetricsTest, MetricsCollection) {
  metrics->startMonitoring();

  // Allow some time for metrics collection
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Metrics should be available (values may vary by platform)
  auto memUsage = metrics->getCurrentMemoryUsage();
  auto cpuUsage = metrics->getCurrentCpuUsage();
  auto processMemUsage = metrics->getProcessMemoryUsage();
  auto processCpuUsage = metrics->getProcessCpuUsage();

  // Basic sanity checks - metrics should be non-negative
  EXPECT_GE(memUsage, 0);
  EXPECT_GE(cpuUsage, 0.0);
  EXPECT_GE(processMemUsage, 0);
  EXPECT_GE(processCpuUsage, 0.0);

  // CPU usage should be within reasonable bounds
  EXPECT_LE(cpuUsage, 100.0);
  EXPECT_LE(processCpuUsage, 100.0);
}

TEST_F(SystemMetricsTest, PeakTracking) {
  metrics->startMonitoring();

  // Allow metrics to be collected
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto initialPeakMem = metrics->getPeakMemoryUsage();
  auto initialPeakCpu = metrics->getPeakCpuUsage();

  // Peak values should be non-negative
  EXPECT_GE(initialPeakMem, 0);
  EXPECT_GE(initialPeakCpu, 0.0);

  // Reset peak usage and verify
  metrics->resetPeakUsage();
  auto resetPeakMem = metrics->getPeakMemoryUsage();
  auto resetPeakCpu = metrics->getPeakCpuUsage();

  // After reset, peak should equal current
  EXPECT_EQ(resetPeakMem, metrics->getCurrentMemoryUsage());
  EXPECT_EQ(resetPeakCpu, metrics->getCurrentCpuUsage());
}

TEST_F(SystemMetricsTest, AlertCallbacks) {
  bool memoryAlertTriggered = false;
  bool cpuAlertTriggered = false;

  metrics->setMemoryAlertCallback([&](size_t current, size_t threshold) {
    memoryAlertTriggered = true;
    EXPECT_GT(current, threshold);
  });

  metrics->setCpuAlertCallback([&](double current, double threshold) {
    cpuAlertTriggered = true;
    EXPECT_GT(current, threshold);
  });

  // Set very low thresholds to trigger alerts
  metrics->setMemoryThreshold(1); // 1 byte
  metrics->setCpuThreshold(0.1);  // 0.1%

  metrics->startMonitoring();

  // Allow some time for monitoring and potential alerts
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Note: Alerts may or may not trigger depending on system state
  // This test validates the callback mechanism works without errors
}

// JobMetricsCollector Tests

TEST_F(JobMetricsCollectorTest, StartStopCollection) {
  EXPECT_FALSE(collector->isCollecting());

  collector->startCollection();
  EXPECT_TRUE(collector->isCollecting());

  collector->stopCollection();
  EXPECT_FALSE(collector->isCollecting());
}

TEST_F(JobMetricsCollectorTest, BasicMetricsCollection) {
  collector->startCollection();

  EXPECT_EQ(collector->getRecordsProcessed(), 0);
  EXPECT_EQ(collector->getRecordsSuccessful(), 0);
  EXPECT_EQ(collector->getRecordsFailed(), 0);

  // Record some processing events
  collector->recordProcessedRecord();
  collector->recordSuccessfulRecord();
  collector->recordProcessedRecord();
  collector->recordFailedRecord();

  EXPECT_EQ(collector->getRecordsProcessed(), 2);
  EXPECT_EQ(collector->getRecordsSuccessful(), 1);
  EXPECT_EQ(collector->getRecordsFailed(), 1);
}

TEST_F(JobMetricsCollectorTest, BatchProcessing) {
  collector->startCollection();

  // Process a batch
  collector->recordBatchProcessed(100, 95, 5);

  EXPECT_EQ(collector->getRecordsProcessed(), 100);
  EXPECT_EQ(collector->getRecordsSuccessful(), 95);
  EXPECT_EQ(collector->getRecordsFailed(), 5);

  // Process another batch
  collector->recordBatchProcessed(50, 48, 2);

  EXPECT_EQ(collector->getRecordsProcessed(), 150);
  EXPECT_EQ(collector->getRecordsSuccessful(), 143);
  EXPECT_EQ(collector->getRecordsFailed(), 7);
}

TEST_F(JobMetricsCollectorTest, ProcessingRateCalculation) {
  collector->startCollection();

  // Record some processing and allow time to pass
  collector->recordBatchProcessed(100, 100, 0);

  // Allow some time for rate calculation
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  collector->updateProcessingRate();

  auto rate = collector->getProcessingRate();
  EXPECT_GE(rate, 0.0); // Rate should be non-negative
}

TEST_F(JobMetricsCollectorTest, ExecutionTimeTracking) {
  collector->startCollection();

  // Allow some execution time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto executionTime = collector->getExecutionTime();
  EXPECT_GE(executionTime.count(), 90);  // Should be at least ~100ms
  EXPECT_LE(executionTime.count(), 200); // But not unreasonably high
}

TEST_F(JobMetricsCollectorTest, MetricsSnapshot) {
  collector->startCollection();

  collector->recordBatchProcessed(50, 45, 5);

  // Allow some time for metrics collection
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto snapshot = collector->getMetricsSnapshot();

  EXPECT_EQ(snapshot.recordsProcessed, 50);
  EXPECT_EQ(snapshot.recordsSuccessful, 45);
  EXPECT_EQ(snapshot.recordsFailed, 5);
  EXPECT_GE(snapshot.executionTime.count(), 40);
  EXPECT_NE(snapshot.timestamp, std::chrono::system_clock::time_point{});
}

TEST_F(JobMetricsCollectorTest, RealTimeUpdates) {
  bool callbackCalled = false;
  std::string callbackJobId;
  JobMetricsCollector::MetricsSnapshot callbackSnapshot;

  auto callback = [&](const std::string &jobId,
                      const JobMetricsCollector::MetricsSnapshot &snapshot) {
    callbackCalled = true;
    callbackJobId = jobId;
    callbackSnapshot = snapshot;
  };

  collector->setMetricsUpdateCallback(callback);
  collector->setUpdateInterval(
      std::chrono::milliseconds(100)); // Fast updates for testing

  collector->startCollection();
  collector->recordBatchProcessed(25, 20, 5);

  // Wait for callback to be triggered
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  EXPECT_TRUE(callbackCalled);
  EXPECT_EQ(callbackJobId, "test_job_123");
  EXPECT_EQ(callbackSnapshot.recordsProcessed, 25);
  EXPECT_EQ(callbackSnapshot.recordsSuccessful, 20);
  EXPECT_EQ(callbackSnapshot.recordsFailed, 5);
}

// JobMetrics Model Tests

class JobMetricsTest : public ::testing::Test {
protected:
  JobMetrics metrics;
};

TEST_F(JobMetricsTest, InitialState) {
  EXPECT_EQ(metrics.recordsProcessed, 0);
  EXPECT_EQ(metrics.recordsSuccessful, 0);
  EXPECT_EQ(metrics.recordsFailed, 0);
  EXPECT_EQ(metrics.processingRate, 0.0);
  EXPECT_EQ(metrics.memoryUsage, 0);
  EXPECT_EQ(metrics.cpuUsage, 0.0);
  EXPECT_EQ(metrics.executionTime.count(), 0);

  // Extended metrics
  EXPECT_EQ(metrics.peakMemoryUsage, 0);
  EXPECT_EQ(metrics.peakCpuUsage, 0.0);
  EXPECT_EQ(metrics.averageProcessingRate, 0.0);
  EXPECT_EQ(metrics.totalBytesProcessed, 0);
  EXPECT_EQ(metrics.totalBytesWritten, 0);
  EXPECT_EQ(metrics.totalBatches, 0);
  EXPECT_EQ(metrics.averageBatchSize, 0.0);

  EXPECT_EQ(metrics.errorRate, 0.0);
  EXPECT_EQ(metrics.consecutiveErrors, 0);
  EXPECT_EQ(metrics.timeToFirstError.count(), 0);

  EXPECT_EQ(metrics.throughputMBps, 0.0);
  EXPECT_EQ(metrics.memoryEfficiency, 0.0);
  EXPECT_EQ(metrics.cpuEfficiency, 0.0);
}

TEST_F(JobMetricsTest, ProcessingRateUpdate) {
  metrics.recordsProcessed = 100;
  metrics.updateProcessingRate(std::chrono::milliseconds(1000)); // 1 second

  EXPECT_EQ(metrics.processingRate, 100.0); // 100 records per second
}

TEST_F(JobMetricsTest, PerformanceIndicators) {
  metrics.recordsProcessed = 1000;
  metrics.recordsSuccessful = 950;
  metrics.recordsFailed = 50;
  metrics.executionTime = std::chrono::milliseconds(5000); // 5 seconds
  metrics.memoryUsage = 1024 * 1024;                       // 1 MB
  metrics.cpuUsage = 50.0;                                 // 50%
  metrics.totalBytesProcessed = 1024 * 1024 * 10;          // 10 MB

  metrics.updatePerformanceIndicators();

  // Check calculated values
  EXPECT_EQ(metrics.errorRate, 5.0); // 50/1000 = 5%
  EXPECT_GT(metrics.throughputMBps, 0.0);
  EXPECT_GT(metrics.memoryEfficiency, 0.0);
  EXPECT_GT(metrics.cpuEfficiency, 0.0);
}

TEST_F(JobMetricsTest, BatchRecording) {
  metrics.recordBatch(100, 95, 5, 1024 * 1024); // 1 MB batch

  EXPECT_EQ(metrics.totalBatches, 1);
  EXPECT_EQ(metrics.totalBytesProcessed, 1024 * 1024);
  EXPECT_EQ(metrics.consecutiveErrors, 0); // Batch had successes

  // Record a failing batch
  metrics.recordBatch(50, 0, 50, 512 * 1024); // All failed

  EXPECT_EQ(metrics.totalBatches, 2);
  EXPECT_EQ(metrics.consecutiveErrors, 50); // All records failed
}

TEST_F(JobMetricsTest, ErrorTracking) {
  metrics.executionTime = std::chrono::milliseconds(1000);

  // Record first error
  metrics.recordError();

  EXPECT_EQ(metrics.consecutiveErrors, 1);
  EXPECT_EQ(metrics.timeToFirstError.count(), 1000);

  // Record more errors
  metrics.recordError();
  metrics.recordError();

  EXPECT_EQ(metrics.consecutiveErrors, 3);
  // Time to first error should remain the same
  EXPECT_EQ(metrics.timeToFirstError.count(), 1000);
}

TEST_F(JobMetricsTest, OverallEfficiency) {
  // Set up decent performance metrics
  metrics.averageProcessingRate = 500.0; // 500 records/sec
  metrics.recordsProcessed = 1000;
  metrics.recordsFailed = 10;        // 1% error rate
  metrics.memoryEfficiency = 1000.0; // 1000 records/MB
  metrics.cpuEfficiency = 50.0;      // 50 records per CPU%

  double efficiency = metrics.getOverallEfficiency();

  EXPECT_GE(efficiency, 0.0);
  EXPECT_LE(efficiency, 1.0);
  EXPECT_GT(efficiency, 0.5); // Should be decent efficiency
}

TEST_F(JobMetricsTest, PerformanceComparison) {
  // Create baseline metrics
  JobMetrics baseline;
  baseline.averageProcessingRate = 1000.0;
  baseline.recordsProcessed = 1000;
  baseline.recordsFailed = 10; // 1% error rate
  baseline.memoryEfficiency = 500.0;
  baseline.cpuEfficiency = 100.0;

  // Create test metrics with similar performance
  metrics.averageProcessingRate = 900.0; // 90% of baseline
  metrics.recordsProcessed = 1000;
  metrics.recordsFailed = 12;       // Slightly higher error rate
  metrics.memoryEfficiency = 450.0; // 90% of baseline
  metrics.cpuEfficiency = 90.0;     // 90% of baseline

  EXPECT_TRUE(metrics.isPerformingWell(baseline));

  // Create poor performing metrics
  metrics.averageProcessingRate = 500.0; // 50% of baseline
  metrics.recordsFailed = 100;           // 10% error rate

  EXPECT_FALSE(metrics.isPerformingWell(baseline));
}

TEST_F(JobMetricsTest, PerformanceSummary) {
  metrics.recordsProcessed = 1000;
  metrics.processingRate = 200.0;
  metrics.errorRate = 2.5;
  metrics.throughputMBps = 15.5;
  metrics.memoryEfficiency = 800.0;

  std::string summary = metrics.getPerformanceSummary();

  EXPECT_FALSE(summary.empty());
  EXPECT_NE(summary.find("1000 records"), std::string::npos);
  EXPECT_NE(summary.find("200.0 rec/sec"), std::string::npos);
  EXPECT_NE(summary.find("2.5% error"), std::string::npos);
  EXPECT_NE(summary.find("15.50 MB/s"), std::string::npos);
}

TEST_F(JobMetricsTest, JsonSerialization) {
  // Set up some test data
  metrics.recordsProcessed = 1000;
  metrics.recordsSuccessful = 950;
  metrics.recordsFailed = 50;
  metrics.processingRate = 200.0;
  metrics.memoryUsage = 1024 * 1024;
  metrics.cpuUsage = 75.5;
  metrics.executionTime = std::chrono::milliseconds(5000);

  // Extended metrics
  metrics.peakMemoryUsage = 2 * 1024 * 1024;
  metrics.peakCpuUsage = 85.0;
  metrics.averageProcessingRate = 180.0;
  metrics.totalBytesProcessed = 10 * 1024 * 1024;
  metrics.totalBytesWritten = 8 * 1024 * 1024;
  metrics.totalBatches = 10;
  metrics.averageBatchSize = 100.0;
  metrics.errorRate = 5.0;
  metrics.consecutiveErrors = 2;
  metrics.timeToFirstError = std::chrono::milliseconds(1000);
  metrics.throughputMBps = 2.0;
  metrics.memoryEfficiency = 1000.0;
  metrics.cpuEfficiency = 13.3;

  // Serialize to JSON
  std::string json = metrics.toJson();

  // Verify JSON contains expected fields
  EXPECT_NE(json.find("\"recordsProcessed\":1000"), std::string::npos);
  EXPECT_NE(json.find("\"recordsSuccessful\":950"), std::string::npos);
  EXPECT_NE(json.find("\"recordsFailed\":50"), std::string::npos);
  EXPECT_NE(json.find("\"processingRate\":200.00"), std::string::npos);
  EXPECT_NE(json.find("\"memoryUsage\":1048576"), std::string::npos);
  EXPECT_NE(json.find("\"cpuUsage\":75.50"), std::string::npos);
  EXPECT_NE(json.find("\"executionTime\":5000"), std::string::npos);

  // Extended fields
  EXPECT_NE(json.find("\"peakMemoryUsage\":2097152"), std::string::npos);
  EXPECT_NE(json.find("\"peakCpuUsage\":85.00"), std::string::npos);
  EXPECT_NE(json.find("\"averageProcessingRate\":180.00"), std::string::npos);
  EXPECT_NE(json.find("\"totalBytesProcessed\":10485760"), std::string::npos);
  EXPECT_NE(json.find("\"totalBytesWritten\":8388608"), std::string::npos);
  EXPECT_NE(json.find("\"totalBatches\":10"), std::string::npos);
  EXPECT_NE(json.find("\"averageBatchSize\":100.00"), std::string::npos);
  EXPECT_NE(json.find("\"errorRate\":5.00"), std::string::npos);
  EXPECT_NE(json.find("\"consecutiveErrors\":2"), std::string::npos);
  EXPECT_NE(json.find("\"timeToFirstError\":1000"), std::string::npos);
  EXPECT_NE(json.find("\"throughputMBps\":2.00"), std::string::npos);
  EXPECT_NE(json.find("\"memoryEfficiency\":1000.00"), std::string::npos);
  EXPECT_NE(json.find("\"cpuEfficiency\":13.30"), std::string::npos);

  // Deserialize and verify
  JobMetrics deserialized = JobMetrics::fromJson(json);

  EXPECT_EQ(deserialized.recordsProcessed, 1000);
  EXPECT_EQ(deserialized.recordsSuccessful, 950);
  EXPECT_EQ(deserialized.recordsFailed, 50);
  EXPECT_NEAR(deserialized.processingRate, 200.0, 0.01);
  EXPECT_EQ(deserialized.memoryUsage, 1024 * 1024);
  EXPECT_NEAR(deserialized.cpuUsage, 75.5, 0.01);
  EXPECT_EQ(deserialized.executionTime.count(), 5000);

  EXPECT_EQ(deserialized.peakMemoryUsage, 2 * 1024 * 1024);
  EXPECT_NEAR(deserialized.peakCpuUsage, 85.0, 0.01);
  EXPECT_NEAR(deserialized.averageProcessingRate, 180.0, 0.01);
  EXPECT_EQ(deserialized.totalBytesProcessed, 10 * 1024 * 1024);
  EXPECT_EQ(deserialized.totalBytesWritten, 8 * 1024 * 1024);
  EXPECT_EQ(deserialized.totalBatches, 10);
  EXPECT_NEAR(deserialized.averageBatchSize, 100.0, 0.01);
  EXPECT_NEAR(deserialized.errorRate, 5.0, 0.01);
  EXPECT_EQ(deserialized.consecutiveErrors, 2);
  EXPECT_EQ(deserialized.timeToFirstError.count(), 1000);
  EXPECT_NEAR(deserialized.throughputMBps, 2.0, 0.01);
  EXPECT_NEAR(deserialized.memoryEfficiency, 1000.0, 0.01);
  EXPECT_NEAR(deserialized.cpuEfficiency, 13.3, 0.01);
}

TEST_F(JobMetricsTest, Reset) {
  // Set up some test data
  metrics.recordsProcessed = 1000;
  metrics.recordsSuccessful = 950;
  metrics.recordsFailed = 50;
  metrics.processingRate = 200.0;
  metrics.memoryUsage = 1024 * 1024;
  metrics.cpuUsage = 75.0;
  metrics.executionTime = std::chrono::milliseconds(5000);

  // Extended metrics
  metrics.peakMemoryUsage = 2 * 1024 * 1024;
  metrics.peakCpuUsage = 85.0;
  metrics.averageProcessingRate = 180.0;
  metrics.totalBytesProcessed = 10 * 1024 * 1024;
  metrics.totalBytesWritten = 8 * 1024 * 1024;
  metrics.totalBatches = 10;
  metrics.averageBatchSize = 100.0;
  metrics.errorRate = 5.0;
  metrics.consecutiveErrors = 2;
  metrics.timeToFirstError = std::chrono::milliseconds(1000);
  metrics.throughputMBps = 2.0;
  metrics.memoryEfficiency = 1000.0;
  metrics.cpuEfficiency = 13.3;

  // Reset metrics
  metrics.reset();

  // Verify everything is reset to initial state
  EXPECT_EQ(metrics.recordsProcessed, 0);
  EXPECT_EQ(metrics.recordsSuccessful, 0);
  EXPECT_EQ(metrics.recordsFailed, 0);
  EXPECT_EQ(metrics.processingRate, 0.0);
  EXPECT_EQ(metrics.memoryUsage, 0);
  EXPECT_EQ(metrics.cpuUsage, 0.0);
  EXPECT_EQ(metrics.executionTime.count(), 0);

  EXPECT_EQ(metrics.peakMemoryUsage, 0);
  EXPECT_EQ(metrics.peakCpuUsage, 0.0);
  EXPECT_EQ(metrics.averageProcessingRate, 0.0);
  EXPECT_EQ(metrics.totalBytesProcessed, 0);
  EXPECT_EQ(metrics.totalBytesWritten, 0);
  EXPECT_EQ(metrics.totalBatches, 0);
  EXPECT_EQ(metrics.averageBatchSize, 0.0);
  EXPECT_EQ(metrics.errorRate, 0.0);
  EXPECT_EQ(metrics.consecutiveErrors, 0);
  EXPECT_EQ(metrics.timeToFirstError.count(), 0);
  EXPECT_EQ(metrics.throughputMBps, 0.0);
  EXPECT_EQ(metrics.memoryEfficiency, 0.0);
  EXPECT_EQ(metrics.cpuEfficiency, 0.0);

  EXPECT_EQ(metrics.startTime, std::chrono::system_clock::time_point{});
  EXPECT_EQ(metrics.lastUpdateTime, std::chrono::system_clock::time_point{});
  EXPECT_EQ(metrics.firstErrorTime, std::chrono::system_clock::time_point{});
}
