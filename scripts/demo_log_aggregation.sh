#!/bin/bash

# Enable strict shell settings for better error handling
set -o errexit -o nounset -o pipefail -o errtrace
trap 'echo "ERROR: line $LINENO: $BASH_COMMAND" >&2' ERR
IFS=$'\n\t'

# Log Aggregation Demo Script
# This script demonstrates the new log aggregation features

# Check for required tools
check_dependencies() {
    if ! command -v jq >/dev/null 2>&1; then
        echo "ERROR: jq is required but not found. Please install jq to run this demo."
        echo "On macOS: brew install jq"
        echo "On Ubuntu/Debian: sudo apt-get install jq"
        exit 1
    fi
}

# Build the test binary if needed
build_test() {
    echo "Building test_log_aggregation..."
    if [ ! -d "build" ]; then
        mkdir -p build
        cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        cd ..
    else
        cd build
        make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        cd ..
    fi
}

# Run the log aggregation test
run_test() {
    echo "Running log aggregation test..."
    if [ ! -x "./build/bin/test_log_aggregation" ]; then
        echo "ERROR: Test binary not found or not executable"
        exit 1
    fi

    ./build/bin/test_log_aggregation
}

# Show the aggregated logs
show_logs() {
    local AGGREGATED_LOG_PATH="logs/test_aggregated.log"

    if [ -f "$AGGREGATED_LOG_PATH" ]; then
        TAIL_LINES="${TAIL_LINES:-200}"
        echo "Aggregated logs (JSON format) [showing last ${TAIL_LINES} lines]:"
        echo "------------------------------"
        # Pretty-print NDJSON: tail last N lines, then slurp into an array
        if tail -n "${TAIL_LINES}" "$AGGREGATED_LOG_PATH" | jq -s '.' 2>/dev/null; then
            echo
        else
            echo "Warning: Could not parse as JSON, showing raw tail content:"
            tail -n "${TAIL_LINES}" "$AGGREGATED_LOG_PATH"
            echo
        fi
        echo "Log file size: $(wc -c < "$AGGREGATED_LOG_PATH") bytes"
        total_entries="$(jq -cs 'length' "$AGGREGATED_LOG_PATH" 2>/dev/null || wc -l < "$AGGREGATED_LOG_PATH")"
        echo "Number of log entries: ${total_entries}"
    else
        echo "No aggregated log file found at $AGGREGATED_LOG_PATH"
    fi
}

# Main execution
main() {
    echo "=== Log Aggregation Demo ==="
    echo

    check_dependencies
    build_test
    run_test
    echo
    show_logs

    echo "Demo completed successfully!"
}

# Run main function
main "$@"