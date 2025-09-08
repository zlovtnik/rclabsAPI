#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>

// Performance testing framework for ETL Plus system
class PerformanceBenchmark {
public:
    struct BenchmarkResult {
        std::string name;
        size_t operations;
        std::chrono::milliseconds duration;
        double throughput; // operations per second
        size_t memoryUsage; // bytes
        double cpuUsage; // percentage
        std::string notes;

        std::string toString() const {
            std::stringstream ss;
            ss << std::left << std::setw(30) << name
               << std::right << std::setw(10) << operations
               << std::setw(8) << duration.count() << "ms"
               << std::setw(12) << std::fixed << std::setprecision(0) << throughput << " ops/sec"
               << std::setw(10) << (memoryUsage / 1024) << "KB"
               << std::setw(8) << std::fixed << std::setprecision(1) << cpuUsage << "%";
            if (!notes.empty()) {
                ss << " (" << notes << ")";
            }
            return ss.str();
        }
    };

    struct SystemMetrics {
        size_t memoryUsage;
        double cpuUsage;
        std::chrono::steady_clock::time_point timestamp;

        SystemMetrics() : memoryUsage(0), cpuUsage(0.0), timestamp(std::chrono::steady_clock::now()) {}
    };

    static SystemMetrics getSystemMetrics() {
        SystemMetrics metrics;

        // Get memory usage (simplified - in real implementation would use platform-specific APIs)
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            size_t pages;
            statm >> pages;
            metrics.memoryUsage = pages * 4096; // Assume 4KB page size
        }

        // Get CPU usage (simplified - in real implementation would track process CPU time)
        metrics.cpuUsage = 0.0; // Placeholder

        return metrics;
    }

    static void printHeader() {
        std::cout << std::string(100, '=') << "\n";
        std::cout << "ETL Plus Performance Benchmark Suite\n";
        std::cout << std::string(100, '=') << "\n\n";

        std::cout << std::left << std::setw(30) << "Benchmark"
                  << std::right << std::setw(10) << "Operations"
                  << std::setw(8) << "Time"
                  << std::setw(12) << "Throughput"
                  << std::setw(10) << "Memory"
                  << std::setw(8) << "CPU" << "\n";
        std::cout << std::string(78, '-') << "\n";
    }

    static void printResult(const BenchmarkResult& result) {
        std::cout << result.toString() << "\n";
    }

    static void printSummary(const std::vector<BenchmarkResult>& results) {
        std::cout << "\n" << std::string(100, '=') << "\n";
        std::cout << "PERFORMANCE SUMMARY\n";
        std::cout << std::string(100, '=') << "\n";

        if (results.empty()) return;

        // Calculate averages
        double avgThroughput = 0.0;
        size_t maxMemory = 0;
        double maxCpu = 0.0;

        for (const auto& result : results) {
            avgThroughput += result.throughput;
            maxMemory = std::max(maxMemory, result.memoryUsage);
            maxCpu = std::max(maxCpu, result.cpuUsage);
        }

        avgThroughput /= results.size();

        std::cout << "Average Throughput: " << std::fixed << std::setprecision(0) << avgThroughput << " ops/sec\n";
        std::cout << "Peak Memory Usage: " << (maxMemory / 1024) << " KB\n";
        std::cout << "Peak CPU Usage: " << std::fixed << std::setprecision(1) << maxCpu << "%\n";

        // Performance recommendations
        std::cout << "\nRECOMMENDATIONS:\n";
        if (avgThroughput < 1000) {
            std::cout << "- Consider optimizing for higher throughput\n";
        }
        if (maxMemory > 100 * 1024 * 1024) { // 100MB
            std::cout << "- High memory usage detected, consider memory optimizations\n";
        }
        if (maxCpu > 80.0) {
            std::cout << "- High CPU usage, consider load distribution or optimization\n";
        }
    }
};

// Base class for all performance benchmarks
class BenchmarkBase {
protected:
    std::string name_;
    std::vector<PerformanceBenchmark::BenchmarkResult> results_;

public:
    BenchmarkBase(const std::string& name) : name_(name) {}
    virtual ~BenchmarkBase() = default;

    virtual void run() = 0;
    virtual void printResults() {
        for (const auto& result : results_) {
            PerformanceBenchmark::printResult(result);
        }
    }

    const std::vector<PerformanceBenchmark::BenchmarkResult>& getResults() const {
        return results_;
    }

    const std::string& getName() const { return name_; }

protected:
    void addResult(const PerformanceBenchmark::BenchmarkResult& result) {
        results_.push_back(result);
    }

    PerformanceBenchmark::BenchmarkResult createResult(
        const std::string& subName,
        size_t operations,
        std::chrono::milliseconds duration,
        const std::string& notes = ""
    ) {
        PerformanceBenchmark::BenchmarkResult result;
        result.name = name_ + " - " + subName;
        result.operations = operations;
        result.duration = duration;
        result.throughput = operations * 1000.0 / std::max(duration.count(), 1LL);
        result.memoryUsage = 0; // Will be filled by system metrics
        result.cpuUsage = 0.0; // Will be filled by system metrics
        result.notes = notes;

        return result;
    }
};
