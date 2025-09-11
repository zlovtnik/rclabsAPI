#!/bin/bash

# Database Connection Pool Load Test Script
# This script tests the database connection pool under various load conditions

set -e

echo "=== Database Connection Pool Load Test ==="

# Configuration
DB_HOST=${DB_HOST:-"localhost"}
DB_PORT=${DB_PORT:-5432}
DB_NAME=${DB_NAME:-"etl_db"}
DB_USER=${DB_USER:-"etl_user"}
if [ -z "$DB_PASSWORD" ]; then
    echo "ERROR: DB_PASSWORD environment variable must be set"
    exit 1
fi

CONCURRENT_CONNECTIONS=${CONCURRENT_CONNECTIONS:-50}
TEST_DURATION=${TEST_DURATION:-60}
QUERIES_PER_CONNECTION=${QUERIES_PER_CONNECTION:-100}

REPORT_FILE="db_pool_load_test_$(date +%Y%m%d_%H%M%S).json"

echo "Database: $DB_HOST:$DB_PORT/$DB_NAME"
echo "Concurrent Connections: $CONCURRENT_CONNECTIONS"
echo "Test Duration: $TEST_DURATION seconds"
echo "Queries per Connection: $QUERIES_PER_CONNECTION"
echo "Report File: $REPORT_FILE"
echo

# Function to run database queries in parallel
run_db_test() {
    local connection_id=$1
    local start_time=$(date +%s.%N)
    local queries_executed=0
    local errors=0

    echo "Connection $connection_id: Starting test..."

    for ((i=1; i<=QUERIES_PER_CONNECTION; i++)); do
        # Test different types of queries
        case $((RANDOM % 4)) in
            0)
                # Simple SELECT
                psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
                     -c "SELECT 1" --quiet --no-align --tuples-only > /dev/null 2>&1
                ;;
            1)
                # Count query
                psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
                     -c "SELECT COUNT(*) FROM information_schema.tables" --quiet --no-align --tuples-only > /dev/null 2>&1
                ;;
            2)
                # System catalog query
                psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
                     -c "SELECT schemaname, tablename FROM pg_tables LIMIT 10" --quiet --no-align --tuples-only > /dev/null 2>&1
                ;;
            3)
                # Sleep query to simulate slow queries
                psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
                     -c "SELECT pg_sleep(0.01)" --quiet --no-align --tuples-only > /dev/null 2>&1
                ;;
        esac

        if [ $? -eq 0 ]; then
            ((queries_executed++))
        else
            ((errors++))
        fi
    done

    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)

    echo "Connection $connection_id: Completed $queries_executed queries with $errors errors in ${duration}s"

    # Return results as JSON
    cat << EOF
{
    "connection_id": $connection_id,
    "queries_executed": $queries_executed,
    "errors": $errors,
    "duration_seconds": $duration,
    "qps": $(echo "scale=2; $queries_executed / $duration" | bc)
}
EOF
}

# Function to monitor system resources
monitor_resources() {
    local duration=$1
    local interval=1
    local samples=$((duration / interval))

    echo "Monitoring system resources for ${duration} seconds..."

    local cpu_samples=()
    local mem_samples=()
    local conn_samples=()

    for ((i=0; i<samples; i++)); do
        # CPU usage - portable detection
        local cpu=0
        if [[ "$OSTYPE" == "darwin"* ]]; then
            # macOS - use top command
            if command -v top >/dev/null 2>&1; then
                cpu=$(top -l 1 | grep "CPU usage" | awk '{print $3}' | sed 's/%//' 2>/dev/null || echo "0")
                # Ensure cpu is numeric, default to 0 if not
                if ! [[ "$cpu" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
                    cpu=0
                fi
            fi
        else
            # Linux - prefer mpstat, fallback to vmstat
            if command -v mpstat >/dev/null 2>&1; then
                # Use mpstat for CPU usage (simpler than /proc/stat parsing)
                cpu=$(mpstat 1 1 2>/dev/null | awk 'NR==4 {print 100 - $NF}' 2>/dev/null || echo "0")
            elif command -v vmstat >/dev/null 2>&1; then
                # Fallback to vmstat - get idle percentage from second sample
                cpu=$(vmstat 1 2 2>/dev/null | awk 'NR==4 {print 100 - $15}' 2>/dev/null || echo "0")
            fi
            # Ensure cpu is numeric, default to 0 if not
            if ! [[ "$cpu" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
                cpu=0
            fi
        fi
        cpu_samples+=("$cpu")

        # Memory usage - portable detection
        local mem
        if [[ "$OSTYPE" == "darwin"* ]]; then
            # macOS - calculate memory usage percentage
            local total_mem=$(sysctl -n hw.memsize)
            local page_size=$(sysctl -n vm.pagesize)
            local pages_active=$(sysctl -n vm.page_pageable_internal_count)
            local pages_inactive=$(sysctl -n vm.page_pageable_external_count)
            local used_pages=$((pages_active + pages_inactive))
            local total_pages=$((total_mem / page_size))
            if [ $total_pages -gt 0 ]; then
                mem=$(awk "BEGIN {printf \"%.2f\", ($used_pages / $total_pages) * 100.0}")
            else
                mem="0.00"
            fi
        else
            # Linux
            mem=$(free | grep Mem | awk '{printf "%.2f", $3/$2 * 100.0}')
        fi
        mem_samples+=("$mem")

        # Database connections - use psql variables to prevent SQL injection
        local connections
        connections=$(PGPASSWORD="$DB_PASSWORD" psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -v user="$DB_USER" -t -c "SELECT count(*) FROM pg_stat_activity WHERE usename = :'user';" 2>/dev/null || echo "0")
        conn_samples+=("$connections")

        sleep $interval
    done

    # Calculate averages and peaks
    local cpu_sum=0
    local mem_sum=0
    local conn_sum=0
    local cpu_peak=0
    local mem_peak=0
    local conn_peak=0

    for cpu in "${cpu_samples[@]}"; do
        cpu_sum=$(echo "$cpu_sum + $cpu" | bc)
        cpu_peak=$(echo "if ($cpu > $cpu_peak) $cpu else $cpu_peak" | bc)
    done

    for mem in "${mem_samples[@]}"; do
        mem_sum=$(echo "$mem_sum + $mem" | bc)
        mem_peak=$(echo "if ($mem > $mem_peak) $mem else $mem_peak" | bc)
    done

    for conn in "${conn_samples[@]}"; do
        conn_sum=$(echo "$conn_sum + $conn" | bc)
        conn_peak=$(echo "if ($conn > $conn_peak) $conn else $conn_peak" | bc)
    done

    local cpu_avg=$(echo "scale=2; $cpu_sum / ${#cpu_samples[@]}" | bc)
    local mem_avg=$(echo "scale=2; $mem_sum / ${#mem_samples[@]}" | bc)
    local conn_avg=$(echo "scale=2; $conn_sum / ${#conn_samples[@]}" | bc)

    cat << EOF
{
    "cpu_usage_avg_percent": $cpu_avg,
    "cpu_usage_peak_percent": $cpu_peak,
    "memory_usage_avg_percent": $mem_avg,
    "memory_usage_peak_percent": $mem_peak,
    "db_connections_avg": $conn_avg,
    "db_connections_peak": $conn_peak
}
EOF
}

# Check if database is accessible
echo "Checking database connectivity..."
PGPASSWORD="$DB_PASSWORD" psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -c "SELECT 1" --quiet > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: Cannot connect to database $DB_HOST:$DB_PORT/$DB_NAME as $DB_USER"
    exit 1
fi
echo "Database connection successful"
echo

# Start resource monitoring in background
monitor_resources $TEST_DURATION > /tmp/resource_monitor_$$.json &
MONITOR_PID=$!

# Run load test
echo "Starting database connection pool load test..."
START_TIME=$(date +%s)

# Run connections in parallel
results=()
for ((i=1; i<=CONCURRENT_CONNECTIONS; i++)); do
    run_db_test $i > /tmp/db_test_$i_$$.json &
    PIDS[$i]=$!
done

# Wait for all connections to complete or timeout
for ((i=1; i<=CONCURRENT_CONNECTIONS; i++)); do
    if [ -n "${PIDS[$i]}" ]; then
        timeout $TEST_DURATION bash -c "wait ${PIDS[$i]}" || true
    fi
done

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

# Wait for monitoring to complete
wait $MONITOR_PID

# Collect results
echo "Collecting test results..."
total_queries=0
total_errors=0
total_duration=0
connection_results=()

for ((i=1; i<=CONCURRENT_CONNECTIONS; i++)); do
    if [ -f "/tmp/db_test_$i_$$.json" ]; then
        result=$(cat "/tmp/db_test_$i_$$.json")
        connection_results+=("$result")

        # Parse JSON for summary
        queries=$(echo "$result" | grep -o '"queries_executed": [0-9]*' | cut -d' ' -f2)
        errors=$(echo "$result" | grep -o '"errors": [0-9]*' | cut -d' ' -f2)
        duration=$(echo "$result" | grep -o '"duration_seconds": [0-9.]*' | cut -d' ' -f2)

        total_queries=$((total_queries + queries))
        total_errors=$((total_errors + errors))
        total_duration=$(echo "$total_duration + $duration" | bc)
    fi
done

# Calculate overall statistics
avg_duration=$(echo "scale=2; $total_duration / $CONCURRENT_CONNECTIONS" | bc)
total_qps=$(echo "scale=2; $total_queries / $DURATION" | bc)
success_rate=$(echo "scale=2; ($total_queries - $total_errors) * 100 / $total_queries" | bc 2>/dev/null || echo "0")

# Get resource monitoring results
resource_data=$(cat "/tmp/resource_monitor_$$.json")

# Generate final report
cat << EOF > "$REPORT_FILE"
{
    "test_info": {
        "database_host": "$DB_HOST",
        "database_port": $DB_PORT,
        "database_name": "$DB_NAME",
        "concurrent_connections": $CONCURRENT_CONNECTIONS,
        "test_duration_seconds": $DURATION,
        "queries_per_connection": $QUERIES_PER_CONNECTION
    },
    "summary": {
        "total_queries": $total_queries,
        "total_errors": $total_errors,
        "success_rate_percent": $success_rate,
        "overall_qps": $total_qps,
        "avg_connection_duration_seconds": $avg_duration
    },
    "resources": $resource_data,
    "connection_results": [
EOF

# Add connection results
first=true
for result in "${connection_results[@]}"; do
    if [ "$first" = true ]; then
        first=false
    else
        echo "," >> "$REPORT_FILE"
    fi
    echo "        $result" >> "$REPORT_FILE"
done

cat << EOF >> "$REPORT_FILE"
    ]
}
EOF

# Print summary
echo
echo "=== Database Connection Pool Load Test Results ==="
echo "Duration: $DURATION seconds"
echo "Concurrent Connections: $CONCURRENT_CONNECTIONS"
echo "Total Queries: $total_queries"
echo "Total Errors: $total_errors"
echo "Success Rate: $success_rate%"
echo "Overall QPS: $total_qps"
echo "Average Connection Duration: ${avg_duration}s"
echo
echo "Detailed report saved to: $REPORT_FILE"

# Cleanup
rm -f /tmp/db_test_*_$$.json /tmp/resource_monitor_$$.json

echo "Test completed successfully!"
