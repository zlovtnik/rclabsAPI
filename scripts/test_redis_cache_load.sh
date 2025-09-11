#!/bin/bash

# Redis Cache Load Test Script
# This script tests the Redis caching layer under various load conditions

set -e

echo "=== Redis Cache Load Test ==="

# Configuration
REDIS_HOST=${REDIS_HOST:-"localhost"}
REDIS_PORT=${REDIS_PORT:-6379}
REDIS_DB=${REDIS_DB:-0}
REDIS_PASSWORD=${REDIS_PASSWORD:-""}

CONCURRENT_CLIENTS=${CONCURRENT_CLIENTS:-20}
TEST_DURATION=${TEST_DURATION:-30}
OPERATIONS_PER_CLIENT=${OPERATIONS_PER_CLIENT:-1000}
CACHE_KEY_PREFIX=${CACHE_KEY_PREFIX:-"test:"}

REPORT_FILE="redis_cache_load_test_$(date +%Y%m%d_%H%M%S).json"

echo "Redis Server: $REDIS_HOST:$REDIS_PORT (DB: $REDIS_DB)"
echo "Concurrent Clients: $CONCURRENT_CLIENTS"
echo "Test Duration: $TEST_DURATION seconds"
echo "Operations per Client: $OPERATIONS_PER_CLIENT"
echo "Report File: $REPORT_FILE"
echo

# Function to test Redis operations
run_cache_test() {
    local client_id=$1
    local start_time=$(date +%s.%N)
    local operations=0
    local hits=0
    local misses=0
    local errors=0

    echo "Client $client_id: Starting cache test..."

    # Build Redis CLI command
    REDIS_CMD="redis-cli -h $REDIS_HOST -p $REDIS_PORT -n $REDIS_DB"
    
    # Set authentication via environment variable to avoid command line exposure
    if [ -n "$REDIS_PASSWORD" ]; then
        export REDISCLI_AUTH="$REDIS_PASSWORD"
    fi

    for ((i=1; i<=OPERATIONS_PER_CLIENT; i++)); do
        local key="${CACHE_KEY_PREFIX}client${client_id}:key$i"
        local value="value$i"

        # Randomly choose operation type
        case $((RANDOM % 4)) in
            0)
                # SET operation
                if $REDIS_CMD SET "$key" "$value" EX 300 > /dev/null 2>&1; then
                    ((operations++))
                else
                    ((errors++))
                fi
                ;;
            1)
                # GET operation
                local result
                if result=$($REDIS_CMD GET "$key" 2>/dev/null); then
                    ((operations++))
                    if [ "$result" != "(nil)" ] && [ -n "$result" ]; then
                        ((hits++))
                    else
                        ((misses++))
                    fi
                else
                    ((misses++))
                fi
                ;;
            2)
                # HSET operation
                if $REDIS_CMD HSET "${key}:hash" field1 "$value" field2 "value2" > /dev/null 2>&1; then
                    ((operations++))
                else
                    ((errors++))
                fi
                ;;
            3)
                # DEL operation
                if $REDIS_CMD DEL "$key" > /dev/null 2>&1; then
                    ((operations++))
                else
                    ((errors++))
                fi
                ;;
        esac
    done

    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)

    echo "Client $client_id: Completed $operations operations ($hits hits, $misses misses, $errors errors) in ${duration}s"

    # Clean up authentication environment variable
    unset REDISCLI_AUTH

    # Return results as JSON
    cat << EOF
{
    "client_id": $client_id,
    "operations_completed": $operations,
    "cache_hits": $hits,
    "cache_misses": $misses,
    "errors": $errors,
    "duration_seconds": $duration,
    "ops_per_second": $(echo "scale=2; $operations / $duration" | bc)
}
EOF
}

# Function to monitor Redis performance
monitor_redis() {
    local duration=$1
    local interval=2
    local samples=$((duration / interval))

    echo "Monitoring Redis performance for ${duration} seconds..."

    local memory_samples=()
    local connections_samples=()
    local ops_samples=()

    for ((i=0; i<samples; i++)); do
        # Get Redis info
        local info
        if [ -n "$REDIS_PASSWORD" ]; then
            info=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -a "$REDIS_PASSWORD" INFO 2>/dev/null)
        else
            info=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" INFO 2>/dev/null)
        fi

        if [ $? -eq 0 ]; then
            # Memory usage
            local memory=$(echo "$info" | grep "used_memory:" | cut -d: -f2)
            memory_samples+=("$memory")

            # Connected clients
            local connections=$(echo "$info" | grep "connected_clients:" | cut -d: -f2)
            connections_samples+=("$connections")

            # Operations per second
            local ops=$(echo "$info" | grep "instantaneous_ops_per_sec:" | cut -d: -f2)
            ops_samples+=("$ops")
        fi

        sleep $interval
    done

    # Calculate statistics
    local memory_sum=0
    local connections_sum=0
    local ops_sum=0
    local memory_peak=0
    local connections_peak=0
    local ops_peak=0

    for mem in "${memory_samples[@]}"; do
        memory_sum=$((memory_sum + mem))
        if [ "$mem" -gt "$memory_peak" ]; then
            memory_peak=$mem
        fi
    done

    for conn in "${connections_samples[@]}"; do
        connections_sum=$((connections_sum + conn))
        if [ "$conn" -gt "$connections_peak" ]; then
            connections_peak=$conn
        fi
    done

    for ops in "${ops_samples[@]}"; do
        ops_sum=$((ops_sum + ops))
        if [ "$ops" -gt "$ops_peak" ]; then
            ops_peak=$ops
        fi
    done

    local count=${#memory_samples[@]}
    if [ "$count" -gt 0 ]; then
        local memory_avg=$((memory_sum / count))
        local connections_avg=$((connections_sum / count))
        local ops_avg=$((ops_sum / count))
    else
        local memory_avg=0
        local connections_avg=0
        local ops_avg=0
    fi

    cat << EOF
{
    "redis_memory_avg_bytes": $memory_avg,
    "redis_memory_peak_bytes": $memory_peak,
    "redis_connections_avg": $connections_avg,
    "redis_connections_peak": $connections_peak,
    "redis_ops_avg_per_sec": $ops_avg,
    "redis_ops_peak_per_sec": $ops_peak
}
EOF
}

# Check if Redis is accessible
echo "Checking Redis connectivity..."
if [ -n "$REDIS_PASSWORD" ]; then
    if ! redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -a "$REDIS_PASSWORD" PING > /dev/null 2>&1; then
        echo "ERROR: Cannot connect to Redis at $REDIS_HOST:$REDIS_PORT"
        exit 1
    fi
else
    if ! redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" PING > /dev/null 2>&1; then
        echo "ERROR: Cannot connect to Redis at $REDIS_HOST:$REDIS_PORT"
        exit 1
    fi
fi
echo "Redis connection successful"
echo

# Clean up any existing test keys
echo "Cleaning up existing test keys..."
if [ -n "$REDIS_PASSWORD" ]; then
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -a "$REDIS_PASSWORD" -n "$REDIS_DB" KEYS "${CACHE_KEY_PREFIX}*" | while read -r key; do
        [ -n "$key" ] && redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -a "$REDIS_PASSWORD" -n "$REDIS_DB" DEL "$key" > /dev/null 2>&1
    done
else
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" KEYS "${CACHE_KEY_PREFIX}*" | while read -r key; do
        [ -n "$key" ] && redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" DEL "$key" > /dev/null 2>&1
    done
fi

# Start Redis monitoring in background
monitor_redis $TEST_DURATION > /tmp/redis_monitor_$$.json &
MONITOR_PID=$!

# Run load test
echo "Starting Redis cache load test..."
START_TIME=$(date +%s)

# Run clients in parallel
results=()
for ((i=1; i<=CONCURRENT_CLIENTS; i++)); do
    run_cache_test $i > /tmp/cache_test_$i_$$.json &
    PIDS[$i]=$!
done

# Wait for all clients to complete or timeout
for ((i=1; i<=CONCURRENT_CLIENTS; i++)); do
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
total_operations=0
total_hits=0
total_misses=0
total_errors=0
total_duration=0
client_results=()

for ((i=1; i<=CONCURRENT_CLIENTS; i++)); do
    if [ -f "/tmp/cache_test_$i_$$.json" ]; then
        result=$(cat "/tmp/cache_test_$i_$$.json")
        client_results+=("$result")

        # Parse JSON for summary
        operations=$(echo "$result" | grep -o '"operations_completed": [0-9]*' | cut -d' ' -f2)
        hits=$(echo "$result" | grep -o '"cache_hits": [0-9]*' | cut -d' ' -f2)
        misses=$(echo "$result" | grep -o '"cache_misses": [0-9]*' | cut -d' ' -f2)
        errors=$(echo "$result" | grep -o '"errors": [0-9]*' | cut -d' ' -f2)
        duration=$(echo "$result" | grep -o '"duration_seconds": [0-9.]*' | cut -d' ' -f2)

        total_operations=$((total_operations + operations))
        total_hits=$((total_hits + hits))
        total_misses=$((total_misses + misses))
        total_errors=$((total_errors + errors))
        total_duration=$(echo "$total_duration + $duration" | bc)
    fi
done

# Calculate overall statistics
avg_duration=$(echo "scale=2; $total_duration / $CONCURRENT_CLIENTS" | bc)
total_ops_per_sec=$(echo "scale=2; $total_operations / $DURATION" | bc)

# Calculate hit rate (avoid division by zero)
if [ $((total_hits + total_misses)) -gt 0 ]; then
    hit_rate=$(echo "scale=2; $total_hits * 100 / ($total_hits + $total_misses)" | bc)
else
    hit_rate="0"
fi

# Calculate success rate (avoid division by zero)
if [ $total_operations -gt 0 ]; then
    success_rate=$(echo "scale=2; ($total_operations - $total_errors) * 100 / $total_operations" | bc)
else
    success_rate="0"
fi

# Get Redis monitoring results
redis_data=$(cat "/tmp/redis_monitor_$$.json")

# Generate final report
cat << EOF > "$REPORT_FILE"
{
    "test_info": {
        "redis_host": "$REDIS_HOST",
        "redis_port": $REDIS_PORT,
        "redis_db": $REDIS_DB,
        "concurrent_clients": $CONCURRENT_CLIENTS,
        "test_duration_seconds": $DURATION,
        "operations_per_client": $OPERATIONS_PER_CLIENT,
        "cache_key_prefix": "$CACHE_KEY_PREFIX"
    },
    "summary": {
        "total_operations": $total_operations,
        "total_cache_hits": $total_hits,
        "total_cache_misses": $total_misses,
        "total_errors": $total_errors,
        "cache_hit_rate_percent": $hit_rate,
        "success_rate_percent": $success_rate,
        "overall_ops_per_second": $total_ops_per_sec,
        "avg_client_duration_seconds": $avg_duration
    },
    "redis_performance": $redis_data,
    "client_results": [
EOF

# Add client results
first=true
for result in "${client_results[@]}"; do
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
echo "=== Redis Cache Load Test Results ==="
echo "Duration: $DURATION seconds"
echo "Concurrent Clients: $CONCURRENT_CLIENTS"
echo "Total Operations: $total_operations"
echo "Cache Hits: $total_hits"
echo "Cache Misses: $total_misses"
echo "Cache Hit Rate: $hit_rate%"
echo "Errors: $total_errors"
echo "Success Rate: $success_rate%"
echo "Overall OPS: $total_ops_per_sec"
echo "Average Client Duration: ${avg_duration}s"
echo
echo "Detailed report saved to: $REPORT_FILE"

# Cleanup
rm -f /tmp/cache_test_*_$$.json /tmp/redis_monitor_$$.json

echo "Test completed successfully!"
