#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <future>
#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// System components
#include "logger.hpp"
#include "config_manager.hpp"
#include "database_manager.hpp"
#include "cache_manager.hpp"
#include "performance_monitor.hpp"
#include "system_metrics.hpp"
#include "redis_cache.hpp"
#include "database_connection_pool.hpp"

// Namespace aliases for convenience
using ETLPlus::Metrics::SystemMetrics;

/**
 * Advanced Load Testing Suite
 *
 * This comprehensive test suite validates system performance under various conditions:
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
    LoadTester(const LoadTestConfig& config) : config_(config) {}
    ~LoadTester() {
        // Cleanup CURL global state
        curl_global_cleanup();
    }

    void runLoadTest() {
        std::cout << "\n=== Advanced Load Testing Suite ===\n";
        std::cout << "Server URL: " << config_.serverUrl << "\n";
        std::cout << "Threads: " << config_.numThreads << "\n";
        std::cout << "Requests per thread: " << config_.requestsPerThread << "\n";
        std::cout << "Test duration: " << config_.testDurationSeconds << " seconds\n\n";

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
        for (auto& thread : workerThreads) {
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

    void initializeComponents() {
        // Initialize database manager
        if (config_.enableDatabaseLoad) {
            ConnectionConfig dbConfig;
            dbConfig.host = std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
            dbConfig.port = std::getenv("DB_PORT") ? std::stoi(std::getenv("DB_PORT")) : 5432;
            dbConfig.database = std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "etl_db";
            dbConfig.username = std::getenv("DB_USER") ? std::getenv("DB_USER") : "etl_user";
            
            const char* dbPassword = std::getenv("DB_PASSWORD");
            if (!dbPassword) {
                std::cerr << "Error: DB_PASSWORD environment variable is required but not set\n";
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

    void workerThread(int threadId) {
        // Ramp up delay
        if (config_.rampUpTimeSeconds > 0) {
            int delay = (threadId * config_.rampUpTimeSeconds * 1000) / config_.numThreads;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }

        for (int i = 0; i < config_.requestsPerThread; ++i) {
            if (!monitoring_.load()) break;

            makeRequest(threadId, i);

            // Small delay between requests to simulate realistic load
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

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

        CURL* curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            CURLcode res = curl_easy_perform(curl);

            auto endTime = std::chrono::steady_clock::now();
            double responseTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

            {
                std::lock_guard<std::mutex> lock(metricsMutex_);
                metrics_.responseTimes.push_back(responseTime);
                metrics_.minResponseTime = std::min(metrics_.minResponseTime, responseTime);
                metrics_.maxResponseTime = std::max(metrics_.maxResponseTime, responseTime);
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

        if (config_.enableCacheLoad && cacheManager_ && cacheManager_->isCacheHealthy()) {
            performCacheLoad();
        }
    }

    void performDatabaseLoad() {
        // Simulate database queries
        metrics_.databaseQueries++;

        // Simple query to test connection pool
        try {
            auto result = dbManager_->selectQuery("SELECT 1");
            if (!result.empty()) {
                // Success
            }
        } catch (const std::exception& e) {
            // Query failed
        }
    }

    void performCacheLoad() {
        // Simulate cache operations with thread-safe RNG
        static thread_local std::mt19937 rng(std::random_device{}());
        static thread_local std::uniform_int_distribution<int> dist(0, 999);
        
        std::string testKey = "test_key_" + std::to_string(dist(rng));
        nlohmann::json testData = {{"test", "data"}, {"timestamp", std::time(nullptr)}};

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
                    metrics_.peakMemoryUsageMB = std::max(metrics_.peakMemoryUsageMB, static_cast<double>(mem) / (1024.0 * 1024.0));
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
                metrics_.peakActiveConnections = std::max(metrics_.peakActiveConnections, activeConns);
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

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
            size_t p95Index = static_cast<size_t>(metrics_.responseTimes.size() * 0.95);
            size_t p99Index = static_cast<size_t>(metrics_.responseTimes.size() * 0.99);

            if (p95Index < metrics_.responseTimes.size()) {
                metrics_.p95ResponseTime = metrics_.responseTimes[p95Index];
            }
            if (p99Index < metrics_.responseTimes.size()) {
                metrics_.p99ResponseTime = metrics_.responseTimes[p99Index];
            }
        }
    }

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
                          metrics_.totalRequests) * 100.0;
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
                                  (metrics_.cacheHits + metrics_.cacheMisses)) * 100.0;
            report["cache"]["hit_rate_percent"] = cacheHitRate;
        }

        report["resources"]["peak_memory_mb"] = metrics_.peakMemoryUsageMB;
        report["resources"]["peak_cpu_percent"] = metrics_.peakCpuUsage;
        report["resources"]["peak_active_connections"] = metrics_.peakActiveConnections;

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
        std::cout << "Throughput: " << report["throughput"]["requests_per_second"] << " req/sec\n";

        if (config_.enableDatabaseLoad) {
            std::cout << "Database Queries: " << metrics_.databaseQueries << "\n";
        }

        if (config_.enableCacheLoad) {
            std::cout << "Cache Hits: " << metrics_.cacheHits << "\n";
            std::cout << "Cache Misses: " << metrics_.cacheMisses << "\n";
            if (report.contains("cache") && report["cache"].contains("hit_rate_percent")) {
                std::cout << "Cache Hit Rate: " << report["cache"]["hit_rate_percent"] << "%\n";
            }
        }

        std::cout << "Peak Memory Usage: " << metrics_.peakMemoryUsageMB << " MB\n";
        std::cout << "Peak CPU Usage: " << metrics_.peakCpuUsage << "%\n";
        std::cout << "Peak Active Connections: " << metrics_.peakActiveConnections << "\n";
    }

    std::mutex metricsMutex_;
};

int main(int argc, char* argv[]) {
    LoadTestConfig config;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--url" && i + 1 < argc) {
            config.serverUrl = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            try {
                config.numThreads = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid value for --threads. Must be a positive integer.\n";
                return 1;
            }
        } else if (arg == "--requests" && i + 1 < argc) {
            try {
                config.requestsPerThread = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid value for --requests. Must be a positive integer.\n";
                return 1;
            }
        } else if (arg == "--duration" && i + 1 < argc) {
            try {
                config.testDurationSeconds = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid value for --duration. Must be a positive integer.\n";
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
        std::cerr << "Error: --threads must be a positive integer (got " << config.numThreads << ")\n";
        return 1;
    }
    if (config.requestsPerThread <= 0) {
        std::cerr << "Error: --requests must be a positive integer (got " << config.requestsPerThread << ")\n";
        return 1;
    }
    if (config.testDurationSeconds <= 0) {
        std::cerr << "Error: --duration must be a positive integer (got " << config.testDurationSeconds << ")\n";
        return 1;
    }

    LoadTester tester(config);
    tester.runLoadTest();

    return 0;
}
