#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

/**
 * @brief Thread-safe performance monitoring class for HTTP server optimization
 *
 * This class provides comprehensive performance metrics collection including
 * request timing, connection reuse rates, timeout tracking, and resource
 * utilization. All operations are thread-safe and designed for high-performance
 * concurrent access.
 */
class PerformanceMonitor {
public:
  /**
   * @brief Enumeration of timeout types for categorized tracking
   */
  enum class TimeoutType {
    CONNECTION, // Connection establishment timeout
    REQUEST     // Request processing timeout
  };

  /**
   * @brief Comprehensive metrics structure
   */
  struct Metrics {
    // Request metrics
    std::atomic<size_t> totalRequests{0};
    std::atomic<size_t> activeRequests{0};
    std::atomic<double> averageResponseTime{0.0};

    // Connection metrics
    std::atomic<size_t> connectionReuses{0};
    std::atomic<size_t> totalConnections{0};

    // Timeout metrics
    std::atomic<size_t> connectionTimeouts{0};
    std::atomic<size_t> requestTimeouts{0};

    // System metrics
    std::chrono::steady_clock::time_point startTime;

    // Calculated metrics (computed on demand)
    double connectionReuseRate{0.0};
    size_t requestsPerSecond{0};

    /**
     * @brief Default constructor initializes start time
     */
    Metrics() : startTime(std::chrono::steady_clock::now()) {}

    /**
     * @brief Copy constructor for thread-safe copying
     */
    Metrics(const Metrics &other)
        : totalRequests(other.totalRequests.load()),
          activeRequests(other.activeRequests.load()),
          averageResponseTime(other.averageResponseTime.load()),
          connectionReuses(other.connectionReuses.load()),
          totalConnections(other.totalConnections.load()),
          connectionTimeouts(other.connectionTimeouts.load()),
          requestTimeouts(other.requestTimeouts.load()),
          startTime(other.startTime),
          connectionReuseRate(other.connectionReuseRate),
          requestsPerSecond(other.requestsPerSecond) {}

    /**
     * @brief Assignment operator for thread-safe assignment
     */
    Metrics &operator=(const Metrics &other) {
      if (this != &other) {
        totalRequests.store(other.totalRequests.load());
        activeRequests.store(other.activeRequests.load());
        averageResponseTime.store(other.averageResponseTime.load());
        connectionReuses.store(other.connectionReuses.load());
        totalConnections.store(other.totalConnections.load());
        connectionTimeouts.store(other.connectionTimeouts.load());
        requestTimeouts.store(other.requestTimeouts.load());
        startTime = other.startTime;
        connectionReuseRate = other.connectionReuseRate;
        requestsPerSecond = other.requestsPerSecond;
      }
      return *this;
    }
  };

  /**
   * @brief Constructor initializes monitoring state
   */
  PerformanceMonitor() = default;

  /**
   * @brief Destructor ensures proper cleanup
   */
  ~PerformanceMonitor() = default;

  // Non-copyable and non-movable for thread safety
  PerformanceMonitor(const PerformanceMonitor &) = delete;
  PerformanceMonitor &operator=(const PerformanceMonitor &) = delete;
  PerformanceMonitor(PerformanceMonitor &&) = delete;
  PerformanceMonitor &operator=(PerformanceMonitor &&) = delete;

  /**
   * @brief Record the start of a request
   * Thread-safe operation that increments active request count
   */
  void recordRequestStart() {
    metrics_.totalRequests.fetch_add(1, std::memory_order_relaxed);
    metrics_.activeRequests.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Record the completion of a request with timing information
   * @param duration The duration of the request processing
   * Thread-safe operation that updates response time statistics
   */
  void recordRequestEnd(std::chrono::milliseconds duration) {
    metrics_.activeRequests.fetch_sub(1, std::memory_order_relaxed);

    // Update average response time using thread-safe approach
    updateAverageResponseTime(duration.count());

    // Store individual response time for detailed analysis
    {
      std::lock_guard<std::mutex> lock(responseTimesMutex_);
      responseTimes_.push_back(duration);

      // Limit stored response times to prevent unbounded growth
      // Keep only the most recent 10000 response times
      if (responseTimes_.size() > 10000) {
        responseTimes_.erase(responseTimes_.begin(),
                             responseTimes_.begin() +
                                 (responseTimes_.size() - 10000));
      }
    }
  }

  /**
   * @brief Record a connection reuse event
   * Thread-safe operation for tracking connection pool efficiency
   */
  void recordConnectionReuse() {
    metrics_.connectionReuses.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Record a new connection creation
   * Thread-safe operation for tracking total connection count
   */
  void recordNewConnection() {
    metrics_.totalConnections.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Record a timeout event
   * @param type The type of timeout that occurred
   * Thread-safe operation for tracking timeout statistics
   */
  void recordTimeout(TimeoutType type) {
    switch (type) {
    case TimeoutType::CONNECTION:
      metrics_.connectionTimeouts.fetch_add(1, std::memory_order_relaxed);
      break;
    case TimeoutType::REQUEST:
      metrics_.requestTimeouts.fetch_add(1, std::memory_order_relaxed);
      break;
    }
  }

  /**
   * @brief Get current metrics snapshot
   * @return Metrics structure with current values and calculated statistics
   * Thread-safe operation that provides consistent snapshot of all metrics
   */
  Metrics getMetrics() const {
    Metrics snapshot = metrics_;

    // Calculate derived metrics
    auto totalConns = snapshot.totalConnections.load();
    if (totalConns > 0) {
      snapshot.connectionReuseRate =
          static_cast<double>(snapshot.connectionReuses.load()) / totalConns;
    }

    // Calculate requests per second
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - snapshot.startTime);
    if (elapsed.count() > 0) {
      snapshot.requestsPerSecond =
          snapshot.totalRequests.load() / elapsed.count();
    }

    return snapshot;
  }

  /**
   * @brief Reset all metrics to initial state
   * Thread-safe operation that clears all collected metrics
   */
  void reset() {
    metrics_.totalRequests.store(0, std::memory_order_relaxed);
    metrics_.activeRequests.store(0, std::memory_order_relaxed);
    metrics_.averageResponseTime.store(0.0, std::memory_order_relaxed);
    metrics_.connectionReuses.store(0, std::memory_order_relaxed);
    metrics_.totalConnections.store(0, std::memory_order_relaxed);
    metrics_.connectionTimeouts.store(0, std::memory_order_relaxed);
    metrics_.requestTimeouts.store(0, std::memory_order_relaxed);
    metrics_.startTime = std::chrono::steady_clock::now();

    {
      std::lock_guard<std::mutex> lock(responseTimesMutex_);
      responseTimes_.clear();
    }
  }

  /**
   * @brief Get detailed response time statistics
   * @return Vector of recent response times for detailed analysis
   * Thread-safe operation that returns copy of response time data
   */
  std::vector<std::chrono::milliseconds> getResponseTimes() const {
    std::lock_guard<std::mutex> lock(responseTimesMutex_);
    return responseTimes_;
  }

  /**
   * @brief Calculate percentile response times
   * @param percentile The percentile to calculate (0.0 to 1.0)
   * @return Response time at the specified percentile
   * Thread-safe operation for statistical analysis
   */
  std::chrono::milliseconds getPercentileResponseTime(double percentile) const {
    if (percentile < 0.0 || percentile > 1.0) {
      return std::chrono::milliseconds{0};
    }

    std::lock_guard<std::mutex> lock(responseTimesMutex_);
    if (responseTimes_.empty()) {
      return std::chrono::milliseconds{0};
    }

    // Create sorted copy for percentile calculation
    auto sortedTimes = responseTimes_;
    std::sort(sortedTimes.begin(), sortedTimes.end());

    size_t index = static_cast<size_t>(percentile * (sortedTimes.size() - 1));
    return sortedTimes[index];
  }

  /**
   * @brief Get metrics in JSON format for external monitoring systems
   * @return JSON string containing all current metrics
   * Thread-safe operation for external monitoring integration
   */
  std::string getMetricsAsJson() const {
    auto metrics = getMetrics();

    std::ostringstream json;
    json << "{\n";
    json << "  \"totalRequests\": " << metrics.totalRequests.load() << ",\n";
    json << "  \"activeRequests\": " << metrics.activeRequests.load() << ",\n";
    json << "  \"averageResponseTime\": " << metrics.averageResponseTime.load()
         << ",\n";
    json << "  \"connectionReuses\": " << metrics.connectionReuses.load()
         << ",\n";
    json << "  \"totalConnections\": " << metrics.totalConnections.load()
         << ",\n";
    json << "  \"connectionTimeouts\": " << metrics.connectionTimeouts.load()
         << ",\n";
    json << "  \"requestTimeouts\": " << metrics.requestTimeouts.load()
         << ",\n";
    json << "  \"connectionReuseRate\": " << metrics.connectionReuseRate
         << ",\n";
    json << "  \"requestsPerSecond\": " << metrics.requestsPerSecond << ",\n";
    json << "  \"p95ResponseTime\": " << getPercentileResponseTime(0.95).count()
         << ",\n";
    json << "  \"p99ResponseTime\": " << getPercentileResponseTime(0.99).count()
         << "\n";
    json << "}";

    return json.str();
  }

  /**
   * @brief Get metrics in Prometheus format for monitoring systems
   * @return Prometheus-formatted metrics string
   * Thread-safe operation for Prometheus integration
   */
  std::string getMetricsAsPrometheus() const {
    auto metrics = getMetrics();

    std::ostringstream prometheus;
    prometheus << "# HELP http_requests_total Total number of HTTP requests\n";
    prometheus << "# TYPE http_requests_total counter\n";
    prometheus << "http_requests_total " << metrics.totalRequests.load()
               << "\n\n";

    prometheus << "# HELP http_requests_active Current number of active HTTP "
                  "requests\n";
    prometheus << "# TYPE http_requests_active gauge\n";
    prometheus << "http_requests_active " << metrics.activeRequests.load()
               << "\n\n";

    prometheus << "# HELP http_request_duration_ms Average HTTP request "
                  "duration in milliseconds\n";
    prometheus << "# TYPE http_request_duration_ms gauge\n";
    prometheus << "http_request_duration_ms "
               << metrics.averageResponseTime.load() << "\n\n";

    prometheus << "# HELP http_connections_reused_total Total number of "
                  "connection reuses\n";
    prometheus << "# TYPE http_connections_reused_total counter\n";
    prometheus << "http_connections_reused_total "
               << metrics.connectionReuses.load() << "\n\n";

    prometheus << "# HELP http_connections_total Total number of connections "
                  "created\n";
    prometheus << "# TYPE http_connections_total counter\n";
    prometheus << "http_connections_total " << metrics.totalConnections.load()
               << "\n\n";

    prometheus << "# HELP http_connection_timeouts_total Total number of "
                  "connection timeouts\n";
    prometheus << "# TYPE http_connection_timeouts_total counter\n";
    prometheus << "http_connection_timeouts_total "
               << metrics.connectionTimeouts.load() << "\n\n";

    prometheus << "# HELP http_request_timeouts_total Total number of request "
                  "timeouts\n";
    prometheus << "# TYPE http_request_timeouts_total counter\n";
    prometheus << "http_request_timeouts_total "
               << metrics.requestTimeouts.load() << "\n\n";

    prometheus << "# HELP http_connection_reuse_rate Connection reuse rate "
                  "(0.0 to 1.0)\n";
    prometheus << "# TYPE http_connection_reuse_rate gauge\n";
    prometheus << "http_connection_reuse_rate " << metrics.connectionReuseRate
               << "\n\n";

    prometheus
        << "# HELP http_requests_per_second Current requests per second\n";
    prometheus << "# TYPE http_requests_per_second gauge\n";
    prometheus << "http_requests_per_second " << metrics.requestsPerSecond
               << "\n\n";

    prometheus << "# HELP http_request_duration_p95_ms 95th percentile request "
                  "duration in milliseconds\n";
    prometheus << "# TYPE http_request_duration_p95_ms gauge\n";
    prometheus << "http_request_duration_p95_ms "
               << getPercentileResponseTime(0.95).count() << "\n\n";

    prometheus << "# HELP http_request_duration_p99_ms 99th percentile request "
                  "duration in milliseconds\n";
    prometheus << "# TYPE http_request_duration_p99_ms gauge\n";
    prometheus << "http_request_duration_p99_ms "
               << getPercentileResponseTime(0.99).count() << "\n";

    return prometheus.str();
  }

private:
  mutable Metrics metrics_;
  mutable std::mutex responseTimesMutex_;
  std::vector<std::chrono::milliseconds> responseTimes_;

  /**
   * @brief Update average response time using exponential moving average
   * @param newResponseTime New response time in milliseconds
   * Thread-safe helper method for maintaining running average
   */
  void updateAverageResponseTime(double newResponseTime) {
    // Use exponential moving average with alpha = 0.1 for smooth updates
    constexpr double alpha = 0.1;

    double currentAvg =
        metrics_.averageResponseTime.load(std::memory_order_relaxed);
    double newAvg;

    // Handle first measurement
    if (currentAvg == 0.0) {
      newAvg = newResponseTime;
    } else {
      newAvg = alpha * newResponseTime + (1.0 - alpha) * currentAvg;
    }

    // Atomic update with compare-and-swap loop for thread safety
    while (!metrics_.averageResponseTime.compare_exchange_weak(
        currentAvg, newAvg, std::memory_order_relaxed)) {
      // Recalculate with updated current average
      if (currentAvg == 0.0) {
        newAvg = newResponseTime;
      } else {
        newAvg = alpha * newResponseTime + (1.0 - alpha) * currentAvg;
      }
    }
  }
};