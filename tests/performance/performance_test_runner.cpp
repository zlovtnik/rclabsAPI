#include "performance_benchmark.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <memory>
#include <vector>
#include <map>

// Forward declarations for benchmark classes
class LoggerBenchmark;
class ConnectionPoolBenchmark;
class WebSocketBenchmark;
class MemoryBenchmark;
class LoadTestBenchmark;

// Performance test runner
class PerformanceTestRunner {
public:
    void runAllBenchmarks() {
        std::cout << "========================================\n";
        std::cout << "ETL Plus Performance Validation Suite\n";
        std::cout << "========================================\n\n";

        // Initialize all benchmarks
        std::vector<std::unique_ptr<BenchmarkBase>> benchmarks;
        benchmarks.emplace_back(std::make_unique<LoggerBenchmark>());
        benchmarks.emplace_back(std::make_unique<ConnectionPoolBenchmark>());
        benchmarks.emplace_back(std::make_unique<WebSocketBenchmark>());
        benchmarks.emplace_back(std::make_unique<MemoryBenchmark>());
        benchmarks.emplace_back(std::make_unique<LoadTestBenchmark>());

        // Run all benchmarks
        for (auto& benchmark : benchmarks) {
            std::cout << "Running " << benchmark->getName() << " benchmarks...\n";
            std::cout << std::string(50, '-') << "\n";

            benchmark->run();

            std::cout << benchmark->getName() << " benchmarks completed.\n\n";
        }

        // Generate comprehensive report
        generateReport(benchmarks);
    }

private:
    void generateReport(const std::vector<std::unique_ptr<BenchmarkBase>>& benchmarks) {
        std::cout << "========================================\n";
        std::cout << "PERFORMANCE VALIDATION REPORT\n";
        std::cout << "========================================\n\n";

        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cout << "Report generated: " << std::ctime(&time) << "\n";

        // Collect all results
        std::vector<PerformanceBenchmark::BenchmarkResult> allResults;
        for (const auto& benchmark : benchmarks) {
            const auto& results = benchmark->getResults();
            allResults.insert(allResults.end(), results.begin(), results.end());
        }

        // Display results by category
        displayResultsByCategory(allResults);

        // Generate performance summary
        generatePerformanceSummary(allResults);

        // Save detailed report to file
        saveDetailedReport(allResults);
    }

    void displayResultsByCategory(const std::vector<PerformanceBenchmark::BenchmarkResult>& results) {
        std::map<std::string, std::vector<PerformanceBenchmark::BenchmarkResult>> categorizedResults;

        for (const auto& result : results) {
            // Extract category from result name (format: "Category - TestName")
            size_t dashPos = result.name.find(" - ");
            std::string category = (dashPos != std::string::npos) ?
                                 result.name.substr(0, dashPos) : "General";
            categorizedResults[category].push_back(result);
        }

        for (const auto& [category, categoryResults] : categorizedResults) {
            std::cout << "Category: " << category << "\n";
            std::cout << std::string(category.length() + 10, '-') << "\n";

            for (const auto& result : categoryResults) {
                std::cout << std::left << std::setw(30) << result.name
                         << std::right << std::setw(10) << result.operations << " ops"
                         << std::right << std::setw(10) << result.duration.count() << " ms"
                         << std::right << std::setw(12) << std::fixed << std::setprecision(2)
                         << result.throughput << " ops/sec"
                         << "  " << result.notes << "\n";
            }
            std::cout << "\n";
        }
    }

    void generatePerformanceSummary(const std::vector<PerformanceBenchmark::BenchmarkResult>& results) {
        std::cout << "PERFORMANCE SUMMARY\n";
        std::cout << "===================\n\n";

        // Calculate overall statistics
        size_t totalOperations = 0;
        std::chrono::milliseconds totalDuration(0);

        for (const auto& result : results) {
            totalOperations += result.operations;
            totalDuration += result.duration;
        }

        double overallOpsPerSecond = static_cast<double>(totalOperations) /
                                   (totalDuration.count() / 1000.0);

        std::cout << "Total Operations: " << totalOperations << "\n";
        std::cout << "Total Duration: " << totalDuration.count() << " ms\n";
        std::cout << "Overall Throughput: " << std::fixed << std::setprecision(2)
                 << overallOpsPerSecond << " operations/second\n\n";

        // Performance thresholds analysis
        analyzePerformanceThresholds(results);
    }

    void analyzePerformanceThresholds(const std::vector<PerformanceBenchmark::BenchmarkResult>& results) {
        std::cout << "PERFORMANCE THRESHOLDS ANALYSIS\n";
        std::cout << "================================\n\n";

        // Define performance thresholds (these would be configurable)
        const std::map<std::string, double> thresholds = {
            {"Logger", 10000.0},      // 10k ops/sec minimum
            {"Connection Pool", 5000.0}, // 5k ops/sec minimum
            {"WebSocket", 2000.0},    // 2k ops/sec minimum
            {"Memory", 100000.0},     // 100k ops/sec minimum
            {"Load Test", 1000.0}     // 1k ops/sec minimum
        };

        std::map<std::string, std::vector<double>> categoryThroughputs;

        for (const auto& result : results) {
            size_t dashPos = result.name.find(" - ");
            std::string category = (dashPos != std::string::npos) ?
                                 result.name.substr(0, dashPos) : "General";
            categoryThroughputs[category].push_back(result.throughput);
        }

        for (const auto& [category, throughputs] : categoryThroughputs) {
            double avgThroughput = 0.0;
            for (double throughput : throughputs) {
                avgThroughput += throughput;
            }
            avgThroughput /= throughputs.size();

            auto it = thresholds.find(category);
            if (it != thresholds.end()) {
                double threshold = it->second;
                std::string status = (avgThroughput >= threshold) ? "PASS" : "FAIL";

                std::cout << std::left << std::setw(15) << category
                         << std::right << std::setw(10) << std::fixed << std::setprecision(2)
                         << avgThroughput << " ops/sec"
                         << std::right << std::setw(10) << threshold << " ops/sec"
                         << std::right << std::setw(8) << status << "\n";
            }
        }
        std::cout << "\n";
    }

    void saveDetailedReport(const std::vector<PerformanceBenchmark::BenchmarkResult>& results) {
        std::ofstream reportFile("performance_report.txt");
        if (!reportFile.is_open()) {
            std::cerr << "Failed to create performance report file.\n";
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        reportFile << "ETL Plus Performance Validation Report\n";
        reportFile << "Generated: " << std::ctime(&time) << "\n";
        reportFile << "========================================\n\n";

        for (const auto& result : results) {
            reportFile << "Test: " << result.name << "\n";
            reportFile << "Operations: " << result.operations << "\n";
            reportFile << "Duration: " << result.duration.count() << " ms\n";
            reportFile << "Throughput: " << std::fixed << std::setprecision(2)
                      << result.throughput << " ops/sec\n";
            reportFile << "Memory Usage: " << (result.memoryUsage / 1024) << " KB\n";
            reportFile << "CPU Usage: " << std::fixed << std::setprecision(1)
                      << result.cpuUsage << "%\n";
            reportFile << "Notes: " << result.notes << "\n";
            reportFile << "----------------------------------------\n";
        }

        reportFile.close();
        std::cout << "Detailed report saved to: performance_report.txt\n\n";
    }
};

// Performance test runner
class PerformanceTestRunner {
public:
    void runAllBenchmarks() {
        std::cout << "========================================\n";
        std::cout << "ETL Plus Performance Validation Suite\n";
        std::cout << "========================================\n\n";

        // Initialize all benchmarks
        std::vector<std::unique_ptr<BenchmarkBase>> benchmarks;
        benchmarks.emplace_back(std::make_unique<LoggerBenchmark>());
        benchmarks.emplace_back(std::make_unique<ConnectionPoolBenchmark>());
        benchmarks.emplace_back(std::make_unique<WebSocketBenchmark>());
        benchmarks.emplace_back(std::make_unique<MemoryBenchmark>());
        benchmarks.emplace_back(std::make_unique<LoadTestBenchmark>());

        // Run all benchmarks
        for (auto& benchmark : benchmarks) {
            std::cout << "Running " << benchmark->getName() << " benchmarks...\n";
            std::cout << std::string(50, '-') << "\n";

            benchmark->run();

            std::cout << benchmark->getName() << " benchmarks completed.\n\n";
        }

        // Generate comprehensive report
        generateReport(benchmarks);
    }

private:
    void generateReport(const std::vector<std::unique_ptr<BenchmarkBase>>& benchmarks) {
        std::cout << "========================================\n";
        std::cout << "PERFORMANCE VALIDATION REPORT\n";
        std::cout << "========================================\n\n";

        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cout << "Report generated: " << std::ctime(&time) << "\n";

        // Collect all results
        std::vector<BenchmarkResult> allResults;
        for (const auto& benchmark : benchmarks) {
            const auto& results = benchmark->getResults();
            allResults.insert(allResults.end(), results.begin(), results.end());
        }

        // Display results by category
        displayResultsByCategory(allResults);

        // Generate performance summary
        generatePerformanceSummary(allResults);

        // Save detailed report to file
        saveDetailedReport(allResults);
    }

    void displayResultsByCategory(const std::vector<BenchmarkResult>& results) {
        std::map<std::string, std::vector<BenchmarkResult>> categorizedResults;

        for (const auto& result : results) {
            categorizedResults[result.category].push_back(result);
        }

        for (const auto& [category, categoryResults] : categorizedResults) {
            std::cout << "Category: " << category << "\n";
            std::cout << std::string(category.length() + 10, '-') << "\n";

            for (const auto& result : categoryResults) {
                std::cout << std::left << std::setw(30) << result.testName
                         << std::right << std::setw(10) << result.operations << " ops"
                         << std::right << std::setw(10) << result.durationMs.count() << " ms"
                         << std::right << std::setw(12) << std::fixed << std::setprecision(2)
                         << (static_cast<double>(result.operations) / result.durationMs.count() * 1000.0) << " ops/sec"
                         << "  " << result.description << "\n";
            }
            std::cout << "\n";
        }
    }

    void generatePerformanceSummary(const std::vector<BenchmarkResult>& results) {
        std::cout << "PERFORMANCE SUMMARY\n";
        std::cout << "===================\n\n";

        // Calculate overall statistics
        size_t totalOperations = 0;
        std::chrono::milliseconds totalDuration(0);

        for (const auto& result : results) {
            totalOperations += result.operations;
            totalDuration += result.durationMs;
        }

        double overallOpsPerSecond = static_cast<double>(totalOperations) /
                                   (totalDuration.count() / 1000.0);

        std::cout << "Total Operations: " << totalOperations << "\n";
        std::cout << "Total Duration: " << totalDuration.count() << " ms\n";
        std::cout << "Overall Throughput: " << std::fixed << std::setprecision(2)
                 << overallOpsPerSecond << " operations/second\n\n";

        // Performance thresholds analysis
        analyzePerformanceThresholds(results);
    }

    void analyzePerformanceThresholds(const std::vector<BenchmarkResult>& results) {
        std::cout << "PERFORMANCE THRESHOLDS ANALYSIS\n";
        std::cout << "================================\n\n";

        // Define performance thresholds (these would be configurable)
        const std::map<std::string, double> thresholds = {
            {"Logger", 10000.0},      // 10k ops/sec minimum
            {"Connection Pool", 5000.0}, // 5k ops/sec minimum
            {"WebSocket", 2000.0},    // 2k ops/sec minimum
            {"Memory", 100000.0},     // 100k ops/sec minimum
            {"Load Test", 1000.0}     // 1k ops/sec minimum
        };

        std::map<std::string, std::vector<double>> categoryThroughputs;

        for (const auto& result : results) {
            double throughput = static_cast<double>(result.operations) /
                              (result.durationMs.count() / 1000.0);
            categoryThroughputs[result.category].push_back(throughput);
        }

        for (const auto& [category, throughputs] : categoryThroughputs) {
            double avgThroughput = 0.0;
            for (double throughput : throughputs) {
                avgThroughput += throughput;
            }
            avgThroughput /= throughputs.size();

            auto it = thresholds.find(category);
            if (it != thresholds.end()) {
                double threshold = it->second;
                std::string status = (avgThroughput >= threshold) ? "PASS" : "FAIL";

                std::cout << std::left << std::setw(15) << category
                         << std::right << std::setw(10) << std::fixed << std::setprecision(2)
                         << avgThroughput << " ops/sec"
                         << std::right << std::setw(10) << threshold << " ops/sec"
                         << std::right << std::setw(8) << status << "\n";
            }
        }
        std::cout << "\n";
    }

    void saveDetailedReport(const std::vector<BenchmarkResult>& results) {
        std::ofstream reportFile("performance_report.txt");
        if (!reportFile.is_open()) {
            std::cerr << "Failed to create performance report file.\n";
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        reportFile << "ETL Plus Performance Validation Report\n";
        reportFile << "Generated: " << std::ctime(&time) << "\n";
        reportFile << "========================================\n\n";

        for (const auto& result : results) {
            reportFile << "Category: " << result.category << "\n";
            reportFile << "Test: " << result.testName << "\n";
            reportFile << "Operations: " << result.operations << "\n";
            reportFile << "Duration: " << result.durationMs.count() << " ms\n";
            reportFile << "Throughput: " << std::fixed << std::setprecision(2)
                      << (static_cast<double>(result.operations) / result.durationMs.count() * 1000.0)
                      << " ops/sec\n";
            reportFile << "Description: " << result.description << "\n";
            reportFile << "----------------------------------------\n";
        }

        reportFile.close();
        std::cout << "Detailed report saved to: performance_report.txt\n\n";
    }
};

int main() {
    try {
        PerformanceTestRunner runner;
        runner.runAllBenchmarks();

        std::cout << "Performance validation completed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Performance validation failed: " << e.what() << "\n";
        return 1;
    }
}
