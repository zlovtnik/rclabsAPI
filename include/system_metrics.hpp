#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace ETLPlus::Metrics {

/**
 * System resource metrics collector
 * Provides real-time memory and CPU usage monitoring for job execution
 */
class SystemMetrics {
public:
  SystemMetrics();
  ~SystemMetrics();

  // Start/stop monitoring
  void startMonitoring();
  void stopMonitoring();
  bool isMonitoring() const;

  // Current resource usage
  size_t getCurrentMemoryUsage() const;
  double getCurrentCpuUsage() const;
  size_t getProcessMemoryUsage() const;
  double getProcessCpuUsage() const;

  // Peak resource usage tracking
  size_t getPeakMemoryUsage() const;
  double getPeakCpuUsage() const;
  void resetPeakUsage();

  // Resource usage deltas since start
  size_t getMemoryUsageDelta() const;
  double getCpuUsageDelta() const;

  // Monitoring configuration
  void setMonitoringInterval(std::chrono::milliseconds interval);
  void setMemoryThreshold(size_t thresholdBytes);
  void setCpuThreshold(double thresholdPercent);

  // Alert callbacks
  using MemoryAlertCallback =
      std::function<void(size_t currentUsage, size_t threshold)>;
  using CpuAlertCallback =
      std::function<void(double currentUsage, double threshold)>;

  void setMemoryAlertCallback(MemoryAlertCallback callback);
  void setCpuAlertCallback(CpuAlertCallback callback);

private:
  mutable std::mutex metricsMutex_;
  std::atomic<bool> monitoring_{false};
  std::thread monitoringThread_;

  // Monitoring configuration
  std::chrono::milliseconds monitoringInterval_{1000}; // 1 second default
  size_t memoryThreshold_{0}; // No threshold by default
  double cpuThreshold_{0.0};  // No threshold by default

  // Current metrics
  std::atomic<size_t> currentMemoryUsage_{0};
  std::atomic<double> currentCpuUsage_{0.0};
  std::atomic<size_t> processMemoryUsage_{0};
  std::atomic<double> processCpuUsage_{0.0};

  // Peak metrics tracking
  std::atomic<size_t> peakMemoryUsage_{0};
  std::atomic<double> peakCpuUsage_{0.0};

  // Baseline metrics for delta calculations
  size_t baselineMemoryUsage_{0};
  double baselineCpuUsage_{0.0};
  bool baselineSet_{false};

  // Alert callbacks
  MemoryAlertCallback memoryAlertCallback_;
  CpuAlertCallback cpuAlertCallback_;

  // Monitoring loop
  void monitoringLoop();

  // Platform-specific implementations
  size_t getSystemMemoryUsage() const;
  double getSystemCpuUsage() const;
  size_t getCurrentProcessMemoryUsage() const;
  double getCurrentProcessCpuUsage() const;

  // Alert checking
  void checkAlertThresholds();

  // Update peak values
  void updatePeakValues();

  // Set baseline values
  void setBaseline();
};

/**
 * Job-specific metrics collector
 * Tracks metrics for individual job execution
 */
class JobMetricsCollector {
public:
  explicit JobMetricsCollector(const std::string &jobId);
  ~JobMetricsCollector();

  // Start/stop collection for this job
  void startCollection();
  void stopCollection();
  bool isCollecting() const;

  // Record processing events
  void recordProcessedRecord();
  void recordSuccessfulRecord();
  void recordFailedRecord();
  void recordBatchProcessed(int batchSize, int successful, int failed);

  // Get current metrics
  int getRecordsProcessed() const;
  int getRecordsSuccessful() const;
  int getRecordsFailed() const;
  double getProcessingRate() const;
  std::chrono::milliseconds getExecutionTime() const;
  size_t getMemoryUsage() const;
  double getCpuUsage() const;

  // Update processing rate calculation
  void updateProcessingRate();

  // Get comprehensive metrics snapshot
  struct MetricsSnapshot {
    int recordsProcessed;
    int recordsSuccessful;
    int recordsFailed;
    double processingRate;
    std::chrono::milliseconds executionTime;
    size_t memoryUsage;
    double cpuUsage;
    std::chrono::system_clock::time_point timestamp;
  };

  MetricsSnapshot getMetricsSnapshot() const;

  // Real-time metrics broadcasting
  using MetricsUpdateCallback = std::function<void(
      const std::string &jobId, const MetricsSnapshot &metrics)>;
  void setMetricsUpdateCallback(MetricsUpdateCallback callback);
  void setUpdateInterval(std::chrono::milliseconds interval);

private:
  std::string jobId_;
  std::shared_ptr<SystemMetrics> systemMetrics_;

  // Collection state
  std::atomic<bool> collecting_{false};
  std::chrono::system_clock::time_point startTime_;

  // Processing counters
  std::atomic<int> recordsProcessed_{0};
  std::atomic<int> recordsSuccessful_{0};
  std::atomic<int> recordsFailed_{0};

  // Performance metrics
  std::atomic<double> processingRate_{0.0};
  std::atomic<std::chrono::system_clock::time_point> lastRateUpdate_;
  std::atomic<int> recordsAtLastUpdate_{0};

  // Resource usage at job start
  size_t baselineMemoryUsage_{0};
  double baselineCpuUsage_{0.0};

  // Real-time updates
  MetricsUpdateCallback updateCallback_;
  std::chrono::milliseconds updateInterval_{5000}; // 5 seconds default
  std::thread updateThread_;
  std::atomic<bool> shouldStopUpdates_{false};

  // Update loop for real-time broadcasting
  void updateLoop();

  // Calculate current execution time
  std::chrono::milliseconds calculateExecutionTime() const;
};

} // namespace ETLPlus::Metrics
