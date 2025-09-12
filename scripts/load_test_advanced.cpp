#include <algorithm>
#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

// System components
#include "cache_manager.hpp"
#include "config_manager.hpp"
#include "database_connection_pool.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include "performance_monitor.hpp"
#include "redis_cache.hpp"
#include "system_metrics.hpp"

// Namespace aliases for convenience
using ETLPlus::Metrics::SystemMetrics;

/**
 * Advanced Load Testing Suite
 *
 * This comprehensive test suite validates system performance under various
 * conditions:
 * 1. High-frequency concurrent API requests
 * 2. Database connection pool stress testing
 * 3. Cache performance under load
 * 4. Memory and CPU usage monitoring
 * 5. System stability over extended periods
 * 6. Resource usage analysis
 */

struct LoadTestConfig {
  std::string serverUrl = "http://localhost:8080";
  int numThreads = 10;
  int requestsPerThread = 100;
  int rampUpTimeSeconds = 5;
  int testDurationSeconds = 60;
  int maxConcurrentConnections = 100;
  bool enableDatabaseLoad = true;
  bool enableCacheLoad = true;
  bool monitorResources = true;
  std::string reportFile = "load_test_report.json";
};

struct LoadTestMetrics {
  std::atomic<uint64_t> totalRequests{0};
  std::atomic<uint64_t> successfulRequests{0};
  std::atomic<uint64_t> failedRequests{0};
  std::atomic<uint64_t> timeoutRequests{0};
  std::atomic<uint64_t> databaseQueries{0};
  std::atomic<uint64_t> cacheHits{0};
  std::atomic<uint64_t> cacheMisses{0};

  std::vector<double> responseTimes;
  std::vector<double> cpuUsage;
  std::vector<double> memoryUsage;
  std::vector<size_t> activeConnections;

  std::chrono::steady_clock::time_point startTime;
  std::chrono::steady_clock::time_point endTime;

  double minResponseTime = std::numeric_limits<double>::max();
  double maxResponseTime = 0.0;
  double avgResponseTime = 0.0;
  double p95ResponseTime = 0.0;
  double p99ResponseTime = 0.0;

  double peakMemoryUsageMB = 0.0;
  double peakCpuUsage = 0.0;
  size_t peakActiveConnections = 0;
};

class LoadTester {
public:
  /**
 * @brief Constructs a LoadTester with the given configuration.
 *
 * Initializes a LoadTester instance using the provided LoadTestConfig which
 * determines test parameters (server URL, thread counts, durations, feature
 * toggles, and report path).
 *
 * @param config Configuration values used to drive the load test; stored by copy.
 */
LoadTester(const LoadTestConfig &config) : config_(config) {}
  /**
   * @brief Destructor for LoadTester.
   *
   * Releases global libcurl resources by calling curl_global_cleanup().
   */
  ~LoadTester() {
    // Cleanup CURL global state
    curl_global_cleanup();
  }

  /**
   * @brief Orchestrates and runs the full load test.
   *
   * Initializes subsystems, starts resource monitoring, launches worker threads to generate HTTP
   * load (with configured ramp-up/delays), waits for completion, stops monitoring, computes
   * aggregated statistics, and emits the final JSON report and console summary.
   *
   * Side effects:
   * - Initializes optional subsystems (CURL, database/cache, system metrics) via initializeComponents().
   * - Spawns a monitoring thread and multiple worker threads that update shared metrics.
   * - Updates metrics_.startTime and metrics_.endTime.
   * - Writes a JSON report and prints a console summary through generateReport().
   *
   * Notes:
   * - Thread synchronization and metrics aggregation are handled internally; callers need only construct
   *   the LoadTester with a configured LoadTestConfig and call this method.
   */
  void runLoadTest() {
    std::cout << "\n=== Advanced Load Testing Suite ===\n";
    std::cout << "Server URL: " << config_.serverUrl << "\n";
    std::cout << "Threads: " << config_.numThreads << "\n";
    std::cout << "Requests per thread: " << config_.requestsPerThread << "\n";
    std::cout << "Test duration: " << config_.testDurationSeconds
              << " seconds\n\n";

    initializeComponents();
    metrics_.startTime = std::chrono::steady_clock::now();

    // Start monitoring thread
    std::thread monitorThread(&LoadTester::monitorResources, this);

    // Start load generation
    std::vector<std::thread> workerThreads;
    for (int i = 0; i < config_.numThreads; ++i) {
      workerThreads.emplace_back(&LoadTester::workerThread, this, i);
    }

    // Wait for all threads to complete
    for (auto &thread : workerThreads) {
      thread.join();
    }

    // Stop monitoring
    monitoring_ = false;
    monitorThread.join();

    metrics_.endTime = std::chrono::steady_clock::now();
    calculateStatistics();
    generateReport();
  }

private:
  LoadTestConfig config_;
  LoadTestMetrics metrics_;
  std::atomic<bool> monitoring_{true};
  std::unique_ptr<DatabaseManager> dbManager_;
  std::unique_ptr<CacheManager> cacheManager_;
  std::unique_ptr<SystemMetrics> systemMetrics_;

  /**
   * @brief Initialize optional subsystems and global libraries required for the load test.
   *
   * Sets up the database manager, cache manager (optionally Redis-backed), system metrics
   * collector, and performs global libcurl initialization.
   *
   * Detailed behavior:
   * - If database load is enabled, reads DB connection parameters from environment
   *   variables (DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD). DB_PASSWORD is
   *   required; if it is missing the function prints an error and returns early
   *   without completing remaining initialization.
   * - If cache load is enabled and the binary was compiled with ETL_ENABLE_REDIS,
   *   a RedisCache is created and provided to CacheManager; if Redis support is not
   *   compiled in, CacheManager is still constructed and a notice is printed.
   * - If resource monitoring is enabled, instantiates the SystemMetrics helper.
   * - Calls curl_global_init(CURL_GLOBAL_ALL) to initialize libcurl for subsequent HTTP use.
   *
   * Side effects:
   * - May print warnings or errors to stdout/stderr on missing environment variables
   *   or failed subsystem initialization.
   * - Initializes global CURL state.
   */
  void initializeComponents() {
    // Initialize database manager
    if (config_.enableDatabaseLoad) {
      ConnectionConfig dbConfig;
      dbConfig.host =
          std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
      dbConfig.port =
          std::getenv("DB_PORT") ? std::stoi(std::getenv("DB_PORT")) : 5432;
      dbConfig.database =
          std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "etl_db";
      dbConfig.username =
          std::getenv("DB_USER") ? std::getenv("DB_USER") : "etl_user";

      const char *dbPassword = std::getenv("DB_PASSWORD");
      if (!dbPassword) {
        std::cerr << "Error: DB_PASSWORD environment variable is required but "
                     "not set\n";
        return;
      }
      dbConfig.password = dbPassword;

      dbManager_ = std::make_unique<DatabaseManager>();
      if (!dbManager_->connect(dbConfig)) {
        std::cout << "Warning: Failed to connect to database\n";
      }
    }

    // Initialize cache manager
    if (config_.enableCacheLoad) {
#if defined(ETL_ENABLE_REDIS) && ETL_ENABLE_REDIS
      RedisConfig redisConfig;
      auto redisCache = std::make_unique<RedisCache>(redisConfig);

      cacheManager_ = std::make_unique<CacheManager>();
      if (!cacheManager_->initialize(std::move(redisCache))) {
        std::cout << "Warning: Failed to initialize cache\n";
      }
#else
      cacheManager_ = std::make_unique<CacheManager>();
      std::cout << "Cache enabled but Redis support not compiled in\n";
#endif
    }

    // Initialize system metrics
    if (config_.monitorResources) {
      systemMetrics_ = std::make_unique<SystemMetrics>();
    }

    // Initialize CURL
    curl_global_init(CURL_GLOBAL_ALL);
  }

  /**
   * @brief Worker loop executed by each load-generator thread.
   *
   * Launches a per-thread ramp-up delay, then issues up to `config_.requestsPerThread`
   * calls to makeRequest() unless monitoring_ is cleared. Inserts a short (10 ms)
   * pause between requests to better simulate realistic client behavior.
   *
   * @param threadId Zero-based index of this worker thread; used to compute the
   *                 staggered ramp-up delay so threads reach full load over
   *                 config_.rampUpTimeSeconds.
   */
  void workerThread(int threadId) {
    // Ramp up delay
    if (config_.rampUpTimeSeconds > 0) {
      int delay =
          (threadId * config_.rampUpTimeSeconds * 1000) / config_.numThreads;
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    for (int i = 0; i < config_.requestsPerThread; ++i) {
      if (!monitoring_.load())
        break;

      makeRequest(threadId, i);

      // Small delay between requests to simulate realistic load
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  /**
   * @brief Executes a single HTTP request for the load test and records metrics.
   *
   * Performs one HTTP call to a randomly chosen endpoint on the configured server,
   * measures response time (ms), updates thread-safe metrics (counts and response
   * time vectors), and classifies the result as successful, timed out, or failed.
   * When enabled and available, may also invoke performDatabaseLoad() and
   * performCacheLoad() to simulate additional subsystem load.
   *
   * Side effects:
   * - Increments metrics_.totalRequests and one of metrics_.successfulRequests,
   *   metrics_.timeoutRequests (+ metrics_.failedRequests), or metrics_.failedRequests.
   * - Appends the measured response time to metrics_.responseTimes and updates
   *   metrics_.minResponseTime / metrics_.maxResponseTime (protected by metricsMutex_).
   * - May interact with dbManager_ and cacheManager_ (if configured and healthy).
   *
   * Notes:
   * - Uses libcurl for the HTTP request with a 10s timeout and no signals.
   * - Uses a thread-local PRNG to select the endpoint.
   */
  void makeRequest(int threadId, int requestId) {
    metrics_.totalRequests++;

    auto startTime = std::chrono::steady_clock::now();

    // Simulate different types of requests
    std::string url = config_.serverUrl;
    std::string endpoint;

    // Randomly select endpoint (thread-local PRNG)
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<int> dis(1, 5);

    switch (dis(gen)) {
    case 1:
      endpoint = "/api/health";
      break;
    case 2:
      endpoint = "/api/monitor/status";
      break;
    case 3:
      endpoint = "/api/jobs";
      break;
    case 4:
      endpoint = "/api/users";
      break;
    case 5:
      endpoint = "/api/metrics";
      break;
    }

    url += endpoint;

    CURL *curl = curl_easy_init();
    if (curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

      CURLcode res = curl_easy_perform(curl);

      auto endTime = std::chrono::steady_clock::now();
      double responseTime =
          std::chrono::duration<double, std::milli>(endTime - startTime)
              .count();

      {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        metrics_.responseTimes.push_back(responseTime);
        metrics_.minResponseTime =
            std::min(metrics_.minResponseTime, responseTime);
        metrics_.maxResponseTime =
            std::max(metrics_.maxResponseTime, responseTime);
      }

      if (res == CURLE_OK) {
        metrics_.successfulRequests++;
      } else if (res == CURLE_OPERATION_TIMEDOUT) {
        metrics_.timeoutRequests++;
        metrics_.failedRequests++;
      } else {
        metrics_.failedRequests++;
      }

      curl_easy_cleanup(curl);
    } else {
      metrics_.failedRequests++;
    }

    // Simulate database/cache operations if enabled
    if (config_.enableDatabaseLoad && dbManager_ && dbManager_->isConnected()) {
      performDatabaseLoad();
    }

    if (config_.enableCacheLoad && cacheManager_ &&
        cacheManager_->isCacheHealthy()) {
      performCacheLoad();
    }
  }

  /**
   * @brief Performs a small simulated database workload to exercise the connection pool.
   *
   * Executes a single lightweight query ("SELECT 1") via the configured DatabaseManager to
   * verify connectivity and exercise the pool. Increments the metrics_.databaseQueries counter
   * for telemetry. Exceptions thrown by the query are caught and suppressed (no exception is propagated).
   *
   * Preconditions:
   * - dbManager_ should be initialized and connected; if it is null or not connected the function
   *   will not perform a query but still increments the database query counter.
   *
   * Side effects:
   * - Increments metrics_.databaseQueries.
   * - May perform I/O via dbManager_->selectQuery.
   */
  void performDatabaseLoad() {
    // Simulate database queries
    metrics_.databaseQueries++;

    // Simple query to test connection pool
    try {
      auto result = dbManager_->selectQuery("SELECT 1");
      if (!result.empty()) {
        // Success
      }
    } catch (const std::exception &e) {
      // Query failed
    }
  }

  /**
   * @brief Simulates a cache workload for a single operation.
   *
   * Generates a thread-local random test key, attempts to retrieve it from the cache,
   * and updates metrics: increments cacheHits on a hit or cacheMisses and stores
   * the test payload in the cache on a miss.
   *
   * Side effects:
   * - Reads from and possibly writes to the cache via cacheManager_.
   * - Increments metrics_.cacheHits or metrics_.cacheMisses.
   */
  void performCacheLoad() {
    // Simulate cache operations with thread-safe RNG
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<int> dist(0, 999);

    std::string testKey = "test_key_" + std::to_string(dist(rng));
    nlohmann::json testData = {{"test", "data"},
                               {"timestamp", std::time(nullptr)}};

    // Try to get from cache first
    auto cachedData = cacheManager_->getCachedData(testKey);
    if (!cachedData.empty()) {
      metrics_.cacheHits++;
    } else {
      metrics_.cacheMisses++;
      // Cache the data
      cacheManager_->cacheData(testKey, testData);
    }
  }

  /**
   * @brief Periodically samples system and connection metrics while monitoring is enabled.
   *
   * Runs a loop driven by the atomic flag `monitoring_`. Once started, the function
   * samples CPU and memory usage from `systemMetrics_` (if available) and records
   * the values into `metrics_` while updating peak CPU and memory; it also samples
   * active database connection counts from `dbManager_` (if available) and records
   * those values and the peak active connections. All updates to non-atomic metric
   * fields are protected by `metricsMutex_`. The loop sleeps for one second between samples.
   *
   * Side effects:
   * - Appends samples to metrics_.cpuUsage, metrics_.memoryUsage, and metrics_.activeConnections.
   * - Updates metrics_.peakCpuUsage, metrics_.peakMemoryUsageMB, and metrics_.peakActiveConnections.
   *
   * This function does not return a value and does not throw. It relies on external
   * components (`systemMetrics_`, `dbManager_`) being valid if present.
   */
  void monitorResources() {
    while (monitoring_.load()) {
      if (systemMetrics_) {
        auto cpu = systemMetrics_->getCurrentCpuUsage();
        auto mem = systemMetrics_->getCurrentMemoryUsage();

        {
          std::lock_guard<std::mutex> lock(metricsMutex_);
          metrics_.cpuUsage.push_back(cpu);
          metrics_.memoryUsage.push_back(mem);
          metrics_.peakCpuUsage = std::max(metrics_.peakCpuUsage, cpu);
          metrics_.peakMemoryUsageMB =
              std::max(metrics_.peakMemoryUsageMB,
                       static_cast<double>(mem) / (1024.0 * 1024.0));
        }
      }

      // Monitor active connections (simplified)
      size_t activeConns = 0;
      if (dbManager_) {
        auto poolMetrics = dbManager_->getPoolMetrics();
        activeConns = poolMetrics.activeConnections;
      }

      {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        metrics_.activeConnections.push_back(activeConns);
        metrics_.peakActiveConnections =
            std::max(metrics_.peakActiveConnections, activeConns);
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  /**
   * @brief Compute summary statistics for collected response times.
   *
   * Calculates average, 95th, and 99th percentile response times from the
   * metrics_.responseTimes sample set and stores the results in the
   * metrics_ structure (avgResponseTime, p95ResponseTime, p99ResponseTime).
   *
   * The function acquires metricsMutex_ before reading/writing shared metric
   * fields. If no response times are available, statistical fields are left
   * unchanged. Percentiles are derived by sorting the sample vector and using
   * the nearest-index method (no interpolation).
   */
  void calculateStatistics() {
    std::lock_guard<std::mutex> lock(metricsMutex_);

    // Calculate average response time
    if (!metrics_.responseTimes.empty()) {
      double sum = 0.0;
      for (double time : metrics_.responseTimes) {
        sum += time;
      }
      metrics_.avgResponseTime = sum / metrics_.responseTimes.size();

      // Calculate percentiles
      std::sort(metrics_.responseTimes.begin(), metrics_.responseTimes.end());
      size_t p95Index =
          static_cast<size_t>(metrics_.responseTimes.size() * 0.95);
      size_t p99Index =
          static_cast<size_t>(metrics_.responseTimes.size() * 0.99);

      if (p95Index < metrics_.responseTimes.size()) {
        metrics_.p95ResponseTime = metrics_.responseTimes[p95Index];
      }
      if (p99Index < metrics_.responseTimes.size()) {
        metrics_.p99ResponseTime = metrics_.responseTimes[p99Index];
      }
    }
  }

  /**
   * @brief Generates a JSON load-test report and prints a console summary.
   *
   * @details
   * Builds a JSON object from the collected metrics (duration, request counts,
   * response-time statistics, throughput, database and cache stats, and peak
   * resource usage), writes it to the file path specified by the test
   * configuration, and emits a human-readable summary to stdout. Cache hit rate
   * is included in the report only when cache activity was recorded. Console
   * output uses snapshot values for atomic counters to present consistent totals.
   */
  void generateReport() {
    nlohmann::json report;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        metrics_.endTime - metrics_.startTime);

    report["test_duration_seconds"] = duration.count();
    report["total_requests"] = metrics_.totalRequests.load();
    report["successful_requests"] = metrics_.successfulRequests.load();
    report["failed_requests"] = metrics_.failedRequests.load();
    report["timeout_requests"] = metrics_.timeoutRequests.load();

    double successRate = 0.0;
    if (metrics_.totalRequests > 0) {
      successRate = (static_cast<double>(metrics_.successfulRequests) /
                     metrics_.totalRequests) *
                    100.0;
    }
    report["success_rate_percent"] = successRate;

    report["response_time"]["min_ms"] = metrics_.minResponseTime;
    report["response_time"]["max_ms"] = metrics_.maxResponseTime;
    report["response_time"]["avg_ms"] = metrics_.avgResponseTime;
    report["response_time"]["p95_ms"] = metrics_.p95ResponseTime;
    report["response_time"]["p99_ms"] = metrics_.p99ResponseTime;

    report["throughput"]["requests_per_second"] =
        static_cast<double>(metrics_.totalRequests) / duration.count();

    report["database"]["queries"] = metrics_.databaseQueries.load();
    report["cache"]["hits"] = metrics_.cacheHits.load();
    report["cache"]["misses"] = metrics_.cacheMisses.load();

    if (metrics_.cacheHits + metrics_.cacheMisses > 0) {
      double cacheHitRate = (static_cast<double>(metrics_.cacheHits) /
                             (metrics_.cacheHits + metrics_.cacheMisses)) *
                            100.0;
      report["cache"]["hit_rate_percent"] = cacheHitRate;
    }

    report["resources"]["peak_memory_mb"] = metrics_.peakMemoryUsageMB;
    report["resources"]["peak_cpu_percent"] = metrics_.peakCpuUsage;
    report["resources"]["peak_active_connections"] =
        metrics_.peakActiveConnections;

    // Write report to file
    std::ofstream file(config_.reportFile);
    if (file.is_open()) {
      file << report.dump(2);
      file.close();
      std::cout << "Load test report saved to: " << config_.reportFile << "\n";
    }

    // Print summary to console
    std::cout << "\n=== Load Test Results ===\n";
    std::cout << "Duration: " << duration.count() << " seconds\n";

    // Use snapshot values when printing atomics to avoid multiple loads
    const auto total = metrics_.totalRequests.load();
    const auto successful = metrics_.successfulRequests.load();
    const auto failed = metrics_.failedRequests.load();
    const auto timeouts = metrics_.timeoutRequests.load();

    std::cout << "Total Requests: " << total << "\n";
    std::cout << "Successful: " << successful << " (" << successRate << "%)\n";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "Timeouts: " << timeouts << "\n";
    std::cout << "Avg Response Time: " << metrics_.avgResponseTime << " ms\n";
    std::cout << "95th Percentile: " << metrics_.p95ResponseTime << " ms\n";
    std::cout << "99th Percentile: " << metrics_.p99ResponseTime << " ms\n";
    std::cout << "Throughput: " << report["throughput"]["requests_per_second"]
              << " req/sec\n";

    if (config_.enableDatabaseLoad) {
      std::cout << "Database Queries: " << metrics_.databaseQueries << "\n";
    }

    if (config_.enableCacheLoad) {
      std::cout << "Cache Hits: " << metrics_.cacheHits << "\n";
      std::cout << "Cache Misses: " << metrics_.cacheMisses << "\n";
      if (report.contains("cache") &&
          report["cache"].contains("hit_rate_percent")) {
        std::cout << "Cache Hit Rate: " << report["cache"]["hit_rate_percent"]
                  << "%\n";
      }
    }

    std::cout << "Peak Memory Usage: " << metrics_.peakMemoryUsageMB << " MB\n";
    std::cout << "Peak CPU Usage: " << metrics_.peakCpuUsage << "%\n";
    std::cout << "Peak Active Connections: " << metrics_.peakActiveConnections
              << "\n";
  }

  std::mutex metricsMutex_;
};

/**
 * @brief Entry point for the advanced load testing utility.
 *
 * Parses command-line options, validates them, constructs a LoadTestConfig,
 * and runs a multi-threaded load test via LoadTester.
 *
 * Supported command-line options:
 *  - --url <serverUrl>         : Target server base URL to test.
 *  - --threads <N>            : Number of worker threads (positive integer).
 *  - --requests <M>           : Requests per thread (positive integer).
 *  - --duration <seconds>     : Total test duration in seconds (positive integer).
 *  - --no-db                  : Disable simulated database workload.
 *  - --no-cache               : Disable simulated cache workload.
 *  - --no-monitor             : Disable resource monitoring.
 *  - --report <file>          : Output JSON report file path.
 *
 * Numeric options are validated to be positive integers; invalid values
 * cause the program to print an error message and exit with status 1.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return int Exit code (0 on success, 1 on invalid command-line input).
 */
int main(int argc, char *argv[]) {
  LoadTestConfig config;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--url" && i + 1 < argc) {
      config.serverUrl = argv[++i];
    } else if (arg == "--threads" && i + 1 < argc) {
      try {
        config.numThreads = std::stoi(argv[++i]);
      } catch (const std::exception &) {
        std::cerr << "Error: Invalid value for --threads. Must be a positive "
                     "integer.\n";
        return 1;
      }
    } else if (arg == "--requests" && i + 1 < argc) {
      try {
        config.requestsPerThread = std::stoi(argv[++i]);
      } catch (const std::exception &) {
        std::cerr << "Error: Invalid value for --requests. Must be a positive "
                     "integer.\n";
        return 1;
      }
    } else if (arg == "--duration" && i + 1 < argc) {
      try {
        config.testDurationSeconds = std::stoi(argv[++i]);
      } catch (const std::exception &) {
        std::cerr << "Error: Invalid value for --duration. Must be a positive "
                     "integer.\n";
        return 1;
      }
    } else if (arg == "--no-db") {
      config.enableDatabaseLoad = false;
    } else if (arg == "--no-cache") {
      config.enableCacheLoad = false;
    } else if (arg == "--no-monitor") {
      config.monitorResources = false;
    } else if (arg == "--report" && i + 1 < argc) {
      config.reportFile = argv[++i];
    }
  }

  // Validate parsed values
  if (config.numThreads <= 0) {
    std::cerr << "Error: --threads must be a positive integer (got "
              << config.numThreads << ")\n";
    return 1;
  }
  if (config.requestsPerThread <= 0) {
    std::cerr << "Error: --requests must be a positive integer (got "
              << config.requestsPerThread << ")\n";
    return 1;
  }
  if (config.testDurationSeconds <= 0) {
    std::cerr << "Error: --duration must be a positive integer (got "
              << config.testDurationSeconds << ")\n";
    return 1;
  }

  LoadTester tester(config);
  tester.runLoadTest();

  return 0;
}
