#!/bin/bash

# Enable strict shell settings for better error handling
set -o errexit -o nounset -o pipefail
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

echo "=== ETL Plus Backend - Log Aggregation Demo ==="
echo

# Check dependencies before proceeding
check_dependencies

# Build the project if needed
if [ ! -f "./build/bin/test_log_aggregation" ]; then
    echo "Building test_log_aggregation..."
    make compile
fi

echo "Running log aggregation test..."
echo

# Run the test
./build/bin/test_log_aggregation

echo
echo "=== Log Aggregation Results ==="
echo

# Read the aggregated log path from config
AGGREGATED_LOG_PATH=$(jq -r '.logging.structured_logging.aggregation.file_path // "logs/aggregated.log"' config/config.json 2>/dev/null || echo "logs/aggregated.log")

# Show the aggregated logs
if [ -f "$AGGREGATED_LOG_PATH" ]; then
    echo "Aggregated logs (JSON format):"
    echo "------------------------------"
    # Use jq to pretty-print NDJSON, fallback to plain cat if jq fails
    if jq -s '.' "$AGGREGATED_LOG_PATH" 2>/dev/null; then
        echo
    else
        echo "Warning: Could not parse as JSON, showing raw content:"
        cat "$AGGREGATED_LOG_PATH"
        echo
    fi
    echo "Log file size: $(wc -c < "$AGGREGATED_LOG_PATH") bytes"
    echo "Number of log entries: $(wc -l < "$AGGREGATED_LOG_PATH")"
else
    echo "No aggregated logs found at: $AGGREGATED_LOG_PATH"
fi

echo
echo "=== Configuration ==="
echo
echo "Log aggregation is configured in config/config.json under 'logging.structured_logging.aggregation'"
echo "Current settings:"

# Use jq for safe JSON parsing with fallbacks
AGGREGATION_ENABLED=$(jq -r '.logging.structured_logging.aggregation.enabled // false' config/config.json 2>/dev/null || echo "Not configured")
FILE_PATH=$(jq -r '.logging.structured_logging.aggregation.file_path // "Not configured"' config/config.json 2>/dev/null || echo "Not configured")

echo "- Enabled: $AGGREGATION_ENABLED"
echo "- File destination: $FILE_PATH"
echo

echo "=== Features Implemented ==="
echo "✅ Structured logging format (JSON)"
echo "✅ Log level configuration"
echo "✅ Log shipping to external systems:"
echo "   - File destination"
echo "   - HTTP endpoints (configurable)"
echo "   - Elasticsearch (configurable)"
echo "   - Syslog (configurable)"
echo "   - AWS CloudWatch (configurable)"
echo "   - Splunk (configurable)"
echo "✅ Batch processing with retry logic"
echo "✅ Component-based filtering"
echo "✅ Real-time aggregation"
echo

echo "Demo completed successfully!"