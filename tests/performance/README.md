# ETL Plus Performance Validation Suite

This directory contains comprehensive performance benchmarks for the ETL Plus system, designed to validate performance improvements and ensure the system meets throughput and latency requirements.

## Overview

The performance validation suite includes benchmarks for:

- **Logger Performance**: Tests logging throughput and concurrent logging scenarios
- **Connection Pool Performance**: Validates database connection pooling efficiency
- **WebSocket Performance**: Measures real-time messaging throughput and latency
- **Memory Usage**: Tracks memory consumption patterns and leak detection
- **Load Testing**: Comprehensive stress testing with mixed workloads

## Running the Benchmarks

### Prerequisites

- CMake 3.16 or higher
- C++17 compatible compiler
- Built ETL Plus library (`etl_common`)

### Build Instructions

1. Navigate to the performance tests directory:

   ```bash
   cd /path/to/rclabsAPI/tests/performance
   ```

2. Configure with CMake:

   ```bash
   cmake -S . -B build
   ```

3. Build the performance tests:

   ```bash
   cmake --build build
   ```

4. Run the performance validation:

   ```bash
   ./build/performance_tests
   ```

### Alternative: Build with main project

You can also build the performance tests as part of the main ETL Plus build:

```bash
cd /path/to/rclabsAPI
cmake -S . -B build
cmake --build build
# Run performance tests
ctest -R performance
```

## Benchmark Categories

### 1. Logger Benchmarks

Tests the component logger performance under various scenarios:

- **Basic Logging**: Sequential logging operations
- **Concurrent Logging**: Multi-threaded logging with different log levels
- **Handler Switching**: Performance impact of switching between log handlers
- **Log Level Filtering**: Performance of log level filtering

### 2. Connection Pool Benchmarks

Validates database connection pool efficiency:

- **Connection Acquisition**: Speed of acquiring connections from pool
- **Concurrent Connections**: Multi-threaded connection usage
- **Connection Reuse**: Efficiency of reusing connections
- **Pool Scaling**: Performance with different pool sizes

### 3. WebSocket Benchmarks

Tests real-time communication performance:

- **Message Throughput**: Broadcasting messages to connected clients
- **Concurrent Clients**: Handling multiple simultaneous WebSocket clients
- **Message Latency**: Round-trip message latency measurements
- **Connection Handling**: Connection establishment and cleanup performance

### 4. Memory Benchmarks

Tracks memory usage patterns:

- **Memory Allocation**: Memory allocation and deallocation performance
- **Memory Leak Detection**: Detection of memory leaks in normal operation
- **Object Pooling**: Efficiency of object reuse patterns
- **Cache Efficiency**: Memory usage of caching mechanisms

### 5. Load Testing Benchmarks

Comprehensive stress testing:

- **Concurrent Requests**: High-concurrency HTTP request handling
- **Mixed Workload**: Combination of different operation types
- **Spike Load**: Sudden increases in load (stress testing)
- **Sustained Load**: Long-duration load testing

## Performance Metrics

Each benchmark measures:

- **Operations per Second**: Throughput measurement
- **Latency**: Response time measurements
- **Memory Usage**: Peak and average memory consumption
- **CPU Usage**: Processor utilization
- **Success Rate**: Percentage of successful operations

## Performance Thresholds

The suite includes configurable performance thresholds:

| Component | Minimum Throughput | Notes |
|-----------|-------------------|--------|
| Logger | 10,000 ops/sec | Basic logging operations |
| Connection Pool | 5,000 ops/sec | Database connection operations |
| WebSocket | 2,000 ops/sec | Real-time messaging |
| Memory | 100,000 ops/sec | Memory management operations |
| Load Test | 1,000 ops/sec | Mixed workload operations |

## Output and Reporting

### Console Output

The benchmarks provide real-time console output showing:

- Progress of each benchmark
- Intermediate results
- Performance summaries
- Pass/Fail status against thresholds

### Detailed Report

A comprehensive report is saved to `performance_report.txt` containing:

- Detailed results for each benchmark
- Performance metrics and statistics
- System resource usage
- Recommendations for optimization

### Example Output

```text
========================================
ETL Plus Performance Validation Suite
========================================

Running Logger benchmarks...
--------------------------------------------------
Running basic logging benchmark...
Running concurrent logging benchmark...
Running handler switching benchmark...
Running log level filtering benchmark...
Logger benchmarks completed.

[... other benchmarks ...]

========================================
PERFORMANCE VALIDATION REPORT
========================================

Category: Logger
--------------
Basic Logging                 10000 ops      234 ms    42735.04 ops/sec
Concurrent Logging            80000 ops      456 ms   175438.60 ops/sec
[... continues ...]

PERFORMANCE SUMMARY
===================
Total Operations: 500000
Total Duration: 2500 ms
Overall Throughput: 200000.00 operations/second

PERFORMANCE THRESHOLDS ANALYSIS
================================
Logger              125586.82 ops/sec  10000.00 ops/sec     PASS
Connection Pool       87543.21 ops/sec   5000.00 ops/sec     PASS
[... continues ...]
```

## Customization

### Modifying Test Parameters

You can customize benchmark parameters by editing the source files:

- Number of operations per test
- Number of threads for concurrent tests
- Test durations
- Performance thresholds

### Adding New Benchmarks

To add new benchmarks:

1. Create a new benchmark class inheriting from `BenchmarkBase`
2. Implement the `run()` method with your test logic
3. Add the benchmark to the `PerformanceTestRunner::runAllBenchmarks()` method
4. Update the CMakeLists.txt file

### Example Custom Benchmark

```cpp
class CustomBenchmark : public BenchmarkBase {
public:
    CustomBenchmark() : BenchmarkBase("Custom") {}

    void run() override {
        // Your benchmark implementation
        benchmarkCustomOperation();
    }

private:
    void benchmarkCustomOperation() {
        // Implement your performance test
    }
};
```

## Troubleshooting

### Common Issues

1. **Build Failures**: Ensure all dependencies are installed and paths are correct
2. **Missing Headers**: Check that include paths point to the correct ETL Plus headers
3. **Performance Variations**: Results may vary based on system load and configuration

### Performance Tuning

- Run benchmarks on a dedicated system for consistent results
- Disable CPU frequency scaling for stable performance measurements
- Ensure sufficient system resources (RAM, CPU cores)
- Run multiple iterations to account for system warm-up

## Integration with CI/CD

The performance tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions step
- name: Run Performance Tests
  run: |
    cd tests/performance
    cmake -S . -B build
    cmake --build build
    ./build/performance_tests

- name: Archive Performance Report
  uses: actions/upload-artifact@v2
  with:
    name: performance-report
    path: tests/performance/performance_report.txt
```

## Contributing

When adding new benchmarks or modifying existing ones:

1. Follow the established patterns in existing benchmark classes
2. Include appropriate error handling
3. Add documentation for new benchmark parameters
4. Update this README with new benchmark descriptions
5. Ensure benchmarks are thread-safe and don't interfere with each other
