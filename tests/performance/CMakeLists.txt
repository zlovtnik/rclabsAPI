# Performance Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(ETL_Plus_Performance_Tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/performance
)

# Source files for benchmarks
set(PERFORMANCE_SOURCES
    performance_benchmark.hpp
    logger_benchmark.cpp
    connection_pool_benchmark.cpp
    websocket_benchmark.cpp
    memory_benchmark.cpp
    load_test_benchmark.cpp
    performance_test_runner.cpp
)

# Create performance test executable
add_executable(performance_tests ${PERFORMANCE_SOURCES})

# Link with the main ETL library
target_link_libraries(performance_tests
    etl_common
    pthread
    # Add other necessary libraries based on your system
    # boost_system
    # boost_thread
    # etc.
)

# Set compiler flags for performance testing
target_compile_options(performance_tests PRIVATE
    -O2
    -DNDEBUG
    -Wall
    -Wextra
)

# Add test target
add_test(NAME performance_tests COMMAND performance_tests)

# Custom target to run performance tests
add_custom_target(run_performance_tests
    COMMAND performance_tests
    DEPENDS performance_tests
    COMMENT "Running ETL Plus performance validation tests"
)
