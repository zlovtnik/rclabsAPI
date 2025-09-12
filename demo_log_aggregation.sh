#!/bin/bash

# Log Aggregation Demo Script
# This script demonstrates the new log aggregation features

echo "=== ETL Plus Backend - Log Aggregation Demo ==="
echo

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

# Show the aggregated logs
if [ -f "logs/aggregated.log" ]; then
    echo "Aggregated logs (JSON format):"
    echo "------------------------------"
    cat logs/aggregated.log | jq . 2>/dev/null || cat logs/aggregated.log
    echo
    echo "Log file size: $(wc -c < logs/aggregated.log) bytes"
    echo "Number of log entries: $(wc -l < logs/aggregated.log)"
else
    echo "No aggregated logs found!"
fi

echo
echo "=== Configuration ==="
echo
echo "Log aggregation is configured in config/config.json under 'logging.structured_logging.aggregation'"
echo "Current settings:"
echo "- Enabled: $(grep -A 5 '"aggregation"' config/config.json | grep '"enabled"' | head -1 | cut -d: -f2 | tr -d ' ,')"
echo "- File destination: $(grep -A 10 '"aggregation"' config/config.json | grep '"file_path"' | head -1 | cut -d: -f2 | tr -d ' ",' 2>/dev/null || echo 'Not configured')"
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