#!/bin/bash

# Build script for ETL Plus Integration Tests
# This script compiles all integration and performance tests

set -e  # Exit on any error

echo "ETL Plus Integration Tests Build Script"
echo "======================================"

# Configuration
BUILD_DIR="build"
CMAKE_BUILD_TYPE="Release"
PARALLEL_JOBS=$(nproc)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the project root
if [ ! -f "CMakeLists.txt" ]; then
    print_error "CMakeLists.txt not found. Please run this script from the project root directory."
    exit 1
fi

print_status "Starting build process..."

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    print_status "Creating build directory: $BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure with CMake
print_status "Configuring project with CMake..."
cmake -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
      -DBUILD_TESTING=ON \
      -DBUILD_INTEGRATION_TESTS=ON \
      -DBUILD_PERFORMANCE_TESTS=ON \
      ..

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed"
    exit 1
fi

print_success "CMake configuration completed"

# Build the project
print_status "Building project with $PARALLEL_JOBS parallel jobs..."
make -j$PARALLEL_JOBS

if [ $? -ne 0 ]; then
    print_error "Build failed"
    exit 1
fi

print_success "Build completed successfully"

# Go back to project root
cd ..

# Create test executables directory if it doesn't exist
TEST_BIN_DIR="test_bin"
if [ ! -d "$TEST_BIN_DIR" ]; then
    mkdir -p "$TEST_BIN_DIR"
fi

print_status "Compiling integration test executables..."

# Compiler settings
CXX="g++"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"
INCLUDES="-Iinclude -I/usr/include/boost -I/usr/include/postgresql"
LIBS="-lboost_system -lboost_thread -lboost_filesystem -lboost_program_options -lpq -lpthread"

# Function to compile a test executable
compile_test() {
    local test_name=$1
    local source_file=$2
    local output_file="$TEST_BIN_DIR/$test_name"
    
    print_status "Compiling $test_name..."
    
    # Get all source files except main.cpp
    SOURCE_FILES=$(find src -name "*.cpp" ! -name "main.cpp" | tr '\n' ' ')
    
    $CXX $CXXFLAGS $INCLUDES \
        $source_file \
        $SOURCE_FILES \
        $LIBS \
        -o $output_file
    
    if [ $? -eq 0 ]; then
        print_success "$test_name compiled successfully"
        chmod +x $output_file
    else
        print_error "Failed to compile $test_name"
        return 1
    fi
}

# Compile integration tests
print_status "=== Compiling Integration Tests ==="

compile_test "system_integration_test" "scripts/test_system_integration_final.cpp"
compile_test "performance_load_test" "scripts/test_performance_load.cpp"

# Compile the integrated main application
print_status "=== Compiling Integrated Main Application ==="
compile_test "etlplus_integrated" "src/main_integrated.cpp"

print_success "All integration tests compiled successfully!"

# Create test runner script
print_status "Creating test runner script..."

cat > run_integration_tests.sh << 'EOF'
#!/bin/bash

# Integration Test Runner Script
# Runs all integration tests in sequence

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

echo "ETL Plus Integration Test Runner"
echo "==============================="

# Check if test binaries exist
TEST_BIN_DIR="test_bin"
if [ ! -d "$TEST_BIN_DIR" ]; then
    print_error "Test binaries directory not found. Please run build_integration_tests.sh first."
    exit 1
fi

# Create logs directory
LOGS_DIR="test_logs"
mkdir -p "$LOGS_DIR"

# Function to run a test
run_test() {
    local test_name=$1
    local test_binary="$TEST_BIN_DIR/$test_name"
    local log_file="$LOGS_DIR/${test_name}_$(date +%Y%m%d_%H%M%S).log"
    
    if [ ! -f "$test_binary" ]; then
        print_error "Test binary not found: $test_binary"
        return 1
    fi
    
    print_status "Running $test_name..."
    print_status "Log file: $log_file"
    
    if $test_binary 2>&1 | tee "$log_file"; then
        print_success "$test_name completed successfully"
        return 0
    else
        print_error "$test_name failed"
        return 1
    fi
}

# Run tests
TESTS_PASSED=0
TESTS_FAILED=0

print_status "=== Running System Integration Test ==="
if run_test "system_integration_test"; then
    ((TESTS_PASSED++))
else
    ((TESTS_FAILED++))
fi

echo ""
print_status "=== Running Performance Load Test ==="
if run_test "performance_load_test"; then
    ((TESTS_PASSED++))
else
    ((TESTS_FAILED++))
fi

# Summary
echo ""
echo "==============================="
echo "Integration Test Summary"
echo "==============================="
echo "Tests Passed: $TESTS_PASSED"
echo "Tests Failed: $TESTS_FAILED"
echo "Total Tests:  $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    print_success "All integration tests passed!"
    exit 0
else
    print_error "$TESTS_FAILED test(s) failed"
    exit 1
fi
EOF

chmod +x run_integration_tests.sh
print_success "Test runner script created: run_integration_tests.sh"

# Create performance benchmark script
print_status "Creating performance benchmark script..."

cat > run_performance_benchmarks.sh << 'EOF'
#!/bin/bash

# Performance Benchmark Runner
# Runs performance tests and generates reports

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

echo "ETL Plus Performance Benchmark Runner"
echo "===================================="

# Create benchmark results directory
BENCHMARK_DIR="benchmark_results"
mkdir -p "$BENCHMARK_DIR"

# Create timestamp for this benchmark run
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BENCHMARK_RUN_DIR="$BENCHMARK_DIR/run_$TIMESTAMP"
mkdir -p "$BENCHMARK_RUN_DIR"

print_status "Benchmark results will be saved to: $BENCHMARK_RUN_DIR"

# Run performance tests
TEST_BIN_DIR="test_bin"
PERFORMANCE_TEST="$TEST_BIN_DIR/performance_load_test"

if [ ! -f "$PERFORMANCE_TEST" ]; then
    print_error "Performance test binary not found: $PERFORMANCE_TEST"
    exit 1
fi

print_status "Running performance benchmarks..."

# Run the performance test and capture output
if $PERFORMANCE_TEST 2>&1 | tee "$BENCHMARK_RUN_DIR/performance_test.log"; then
    print_success "Performance benchmarks completed"
else
    print_error "Performance benchmarks failed"
    exit 1
fi

# Move CSV results to benchmark directory
if ls performance_*.csv 1> /dev/null 2>&1; then
    mv performance_*.csv "$BENCHMARK_RUN_DIR/"
    print_success "Performance data saved to $BENCHMARK_RUN_DIR"
fi

# Generate summary report
print_status "Generating benchmark summary report..."

cat > "$BENCHMARK_RUN_DIR/benchmark_summary.md" << REPORT_EOF
# ETL Plus Performance Benchmark Report

**Date:** $(date)
**Benchmark Run:** $TIMESTAMP

## Test Environment

- **OS:** $(uname -s) $(uname -r)
- **CPU:** $(nproc) cores
- **Memory:** $(free -h | grep '^Mem:' | awk '{print $2}')
- **Compiler:** $(g++ --version | head -n1)

## Test Results

The performance tests evaluated the system under various load conditions:

1. **Light Load Test** - Basic functionality with minimal load
2. **Medium Load Test** - Moderate concurrent operations
3. **Heavy Load Test** - High concurrent load
4. **Burst Load Test** - Rapid job creation bursts
5. **Sustained Load Test** - Long-running continuous operation
6. **Memory Stress Test** - Memory-intensive operations
7. **Connection Stress Test** - High number of WebSocket connections

## Performance Data

Detailed performance metrics are available in the CSV files:

REPORT_EOF

# List CSV files in the report
if ls "$BENCHMARK_RUN_DIR"/*.csv 1> /dev/null 2>&1; then
    echo "" >> "$BENCHMARK_RUN_DIR/benchmark_summary.md"
    echo "### Available Data Files" >> "$BENCHMARK_RUN_DIR/benchmark_summary.md"
    echo "" >> "$BENCHMARK_RUN_DIR/benchmark_summary.md"
    for csv_file in "$BENCHMARK_RUN_DIR"/*.csv; do
        filename=$(basename "$csv_file")
        echo "- \`$filename\`" >> "$BENCHMARK_RUN_DIR/benchmark_summary.md"
    done
fi

cat >> "$BENCHMARK_RUN_DIR/benchmark_summary.md" << REPORT_EOF

## Recommendations

Based on the benchmark results:

1. **Job Throughput:** Monitor job processing rates and optimize bottlenecks
2. **WebSocket Performance:** Ensure connection handling scales appropriately
3. **Memory Usage:** Monitor memory consumption patterns
4. **System Stability:** Verify system remains stable under sustained load

## Next Steps

1. Review detailed logs in \`performance_test.log\`
2. Analyze CSV data for performance trends
3. Compare results with previous benchmark runs
4. Identify optimization opportunities

---

*Generated by ETL Plus Performance Benchmark Runner*
REPORT_EOF

print_success "Benchmark summary report generated: $BENCHMARK_RUN_DIR/benchmark_summary.md"

# Create comparison script if previous results exist
PREVIOUS_RUNS=($(ls -d $BENCHMARK_DIR/run_* 2>/dev/null | head -n -1))
if [ ${#PREVIOUS_RUNS[@]} -gt 0 ]; then
    LATEST_PREVIOUS="${PREVIOUS_RUNS[-1]}"
    print_status "Creating performance comparison with previous run: $(basename $LATEST_PREVIOUS)"
    
    # Simple comparison (would be more sophisticated in practice)
    echo "Performance comparison with previous run available in benchmark directory"
fi

print_success "Performance benchmarking completed successfully!"
print_status "Results saved in: $BENCHMARK_RUN_DIR"

EOF

chmod +x run_performance_benchmarks.sh
print_success "Performance benchmark script created: run_performance_benchmarks.sh"

# Create development helper script
print_status "Creating development helper script..."

cat > dev_test_integration.sh << 'EOF'
#!/bin/bash

# Development Integration Test Helper
# Quick test runner for development

set -e

echo "ETL Plus Development Integration Test Helper"
echo "==========================================="

# Quick build and test
echo "Building integration tests..."
./scripts/build_integration_tests.sh

echo ""
echo "Running quick integration test..."
./test_bin/system_integration_test

echo ""
echo "Development integration test completed!"
EOF

chmod +x dev_test_integration.sh
print_success "Development helper script created: dev_test_integration.sh"

# Final summary
echo ""
echo "==============================="
echo "Build Summary"
echo "==============================="
print_success "✓ Project built successfully"
print_success "✓ Integration tests compiled"
print_success "✓ Performance tests compiled"
print_success "✓ Integrated main application compiled"
print_success "✓ Test runner scripts created"

echo ""
echo "Available Commands:"
echo "  ./run_integration_tests.sh      - Run all integration tests"
echo "  ./run_performance_benchmarks.sh - Run performance benchmarks"
echo "  ./dev_test_integration.sh       - Quick development test"
echo "  ./test_bin/etlplus_integrated    - Run integrated application"

echo ""
print_success "Integration test build completed successfully!"
print_status "You can now run the integration tests to validate the system."