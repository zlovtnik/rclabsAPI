#!/bin/bash

# ETL Plus Monitoring and Alerting Script
# Monitors refactored components and sends alerts when issues are detected

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LOG_FILE="$PROJECT_ROOT/logs/monitoring_$(date +%Y%m%d).log"
ALERT_LOG="$PROJECT_ROOT/logs/alerts_$(date +%Y%m%d).log"

# Configuration
MONITORING_INTERVAL=60  # seconds
ALERT_EMAIL="admin@etlplus.local"
SLACK_WEBHOOK_URL=""  # Set this for Slack alerts
HEALTH_CHECK_TIMEOUT=30

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Thresholds for alerts
MEMORY_THRESHOLD=85  # percent
CPU_THRESHOLD=80     # percent
DISK_THRESHOLD=90    # percent
ERROR_RATE_THRESHOLD=5  # errors per minute

# Monitoring state (using indexed arrays for bash compatibility)
COMPONENT_KEYS=("service" "websocket" "database")
COMPONENT_VALUES=("UNKNOWN" "UNKNOWN" "UNKNOWN")

# Set component status
set_component_status() {
    local component="$1"
    local status="$2"
    for i in "${!COMPONENT_KEYS[@]}"; do
        if [[ "${COMPONENT_KEYS[$i]}" == "$component" ]]; then
            COMPONENT_VALUES[$i]="$status"
            break
        fi
    done
}

# Get component status
get_component_status() {
    local component="$1"
    for i in "${!COMPONENT_KEYS[@]}"; do
        if [[ "${COMPONENT_KEYS[$i]}" == "$component" ]]; then
            echo "${COMPONENT_VALUES[$i]}"
            return
        fi
    done
    echo "UNKNOWN"
}

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

log_alert() {
    local message="$1"
    local severity="${2:-WARNING}"

    echo -e "${RED}[ALERT-$severity]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $message" | tee -a "$ALERT_LOG"

    # Send email alert
    send_email_alert "$message" "$severity"

    # Send Slack alert if configured
    if [[ -n "$SLACK_WEBHOOK_URL" ]]; then
        send_slack_alert "$message" "$severity"
    fi
}

# Send email alert
send_email_alert() {
    local message="$1"
    local severity="$2"

    if command -v mail &> /dev/null; then
        echo "$message" | mail -s "ETL Plus Alert [$severity] - $(hostname)" "$ALERT_EMAIL" 2>/dev/null || true
    fi
}

# Send Slack alert
send_slack_alert() {
    local message="$1"
    local severity="$2"

    local color="warning"
    [[ "$severity" == "CRITICAL" ]] && color="danger"
    [[ "$severity" == "INFO" ]] && color="good"

    local payload="{
        \"attachments\": [{
            \"color\": \"$color\",
            \"title\": \"ETL Plus Alert [$severity]\",
            \"text\": \"$message\",
            \"footer\": \"$(hostname)\",
            \"ts\": $(date +%s)
        }]
    }"

    curl -s -X POST -H 'Content-type: application/json' --data "$payload" "$SLACK_WEBHOOK_URL" || true
}

# Check system resources
check_system_resources() {
    log_info "Checking system resources..."

    # Memory usage (macOS compatible)
    local mem_usage=$(echo "scale=2; $(vm_stat | grep 'Pages active' | awk '{print $3}' | tr -d '.') * 4096 / $(sysctl -n hw.memsize) * 100" | bc 2>/dev/null || echo "0")
    if (( $(echo "$mem_usage > $MEMORY_THRESHOLD" | bc -l 2>/dev/null || echo "0") )); then
        log_alert "High memory usage: ${mem_usage}% (threshold: ${MEMORY_THRESHOLD}%)" "WARNING"
    fi

    # CPU usage (macOS compatible)
    local cpu_usage=$(ps -A -o %cpu | awk '{s+=$1} END {print s}')
    if (( $(echo "$cpu_usage > $CPU_THRESHOLD" | bc -l 2>/dev/null || echo "0") )); then
        log_alert "High CPU usage: ${cpu_usage}% (threshold: ${CPU_THRESHOLD}%)" "WARNING"
    fi

    # Disk usage (macOS compatible)
    local disk_usage=$(df / | tail -1 | awk '{print $5}' | sed 's/%//')
    if (( disk_usage > DISK_THRESHOLD )); then
        log_alert "High disk usage: ${disk_usage}% (threshold: ${DISK_THRESHOLD}%)" "CRITICAL"
    fi
}

# Check ETL Plus service health
check_service_health() {
    log_info "Checking ETL Plus service health..."

    # Check if service is running
    if ! pgrep -f "etlplus" > /dev/null; then
        log_alert "ETL Plus service is not running" "CRITICAL"
        set_component_status "service" "DOWN"
        return 1
    fi

    set_component_status "service" "UP"
    log_info "ETL Plus service is running"
}

# Check WebSocket connections
check_websocket_health() {
    log_info "Checking WebSocket health..."

    # Check if WebSocket port is listening
    if ! netstat -tln | grep ":8081 " > /dev/null; then
        log_alert "WebSocket service not listening on port 8081" "WARNING"
        set_component_status "websocket" "DOWN"
        return 1
    fi

    set_component_status "websocket" "UP"
    log_info "WebSocket service is healthy"
}

# Check database connections
check_database_health() {
    log_info "Checking database health..."

    # This would need to be customized based on your database setup
    # For now, just check if we can connect to the configured database
    if [[ -f "$PROJECT_ROOT/config/config.json" ]]; then
        local db_host=$(grep -o '"host": "[^"]*"' "$PROJECT_ROOT/config/config.json" | cut -d'"' -f4)
        local db_port=$(grep -o '"port": [0-9]*' "$PROJECT_ROOT/config/config.json" | cut -d' ' -f2 | tr -d ',')

        if [[ -n "$db_host" && -n "$db_port" ]]; then
            if timeout 5 bash -c "echo > /dev/tcp/$db_host/$db_port" 2>/dev/null; then
                set_component_status "database" "UP"
                log_info "Database connection is healthy"
            else
                log_alert "Cannot connect to database at $db_host:$db_port" "WARNING"
                set_component_status "database" "DOWN"
            fi
        else
            log_warn "Database configuration not found"
            set_component_status "database" "UNKNOWN"
        fi
    fi
}

# Check log files for errors
check_log_errors() {
    log_info "Checking log files for errors..."

    local current_time=$(date +%s)
    local check_window=300  # 5 minutes

    # Check main application log
    if [[ -f "$PROJECT_ROOT/logs/etlplus.log" ]]; then
        local recent_errors=$(tail -n 1000 "$PROJECT_ROOT/logs/etlplus.log" | \
                             grep -i "error\|exception\|critical\|fatal" | \
                             awk -v now="$current_time" -v window="$check_window" \
                             '$0 ~ /[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}/ {
                                 split($1 " " $2, dt, "-");
                                 ts = mktime(dt[1] " " dt[2] " " dt[3] " " $2);
                                 if (now - ts <= window) print
                             }' | wc -l)

        if (( recent_errors > ERROR_RATE_THRESHOLD )); then
            log_alert "High error rate detected: $recent_errors errors in last 5 minutes" "WARNING"
        fi
    fi
}

# Check feature flag status
check_feature_flags() {
    log_info "Checking feature flag status..."

    if [[ -f "$PROJECT_ROOT/config/feature_flags.json" ]]; then
        # Check for any features that might be causing issues
        local enabled_features=$(grep -o '"[^"]*": true' "$PROJECT_ROOT/config/feature_flags.json" | wc -l)

        if (( enabled_features > 0 )); then
            log_info "$enabled_features refactored features are currently enabled"
        else
            log_info "All features are in legacy mode (safe state)"
        fi
    else
        log_warn "Feature flags configuration not found"
    fi
}

# Generate monitoring report
generate_report() {
    log_info "Generating monitoring report..."

    local report_file="$PROJECT_ROOT/logs/monitoring_report_$(date +%Y%m%d_%H%M%S).txt"

    {
        echo "ETL Plus Monitoring Report"
        echo "Generated: $(date)"
        echo "=========================="
        echo ""
        echo "Component Status:"
        for i in "${!COMPONENT_KEYS[@]}"; do
            component="${COMPONENT_KEYS[$i]}"
            status="${COMPONENT_VALUES[$i]}"
            echo "  $component: $status"
        done
        echo ""
        echo "System Resources:"
        echo "  Memory: $(echo "scale=2; $(vm_stat | grep 'Pages active' | awk '{print $3}' | tr -d '.') * 4096 / $(sysctl -n hw.memsize) * 100" | bc 2>/dev/null || echo "N/A")%"
        echo "  CPU: $(ps -A -o %cpu | awk '{s+=$1} END {print s}')%"
        echo "  Disk: $(df / | tail -1 | awk '{print $5}')"
        echo ""
        echo "Recent Alerts:"
        if [[ -f "$ALERT_LOG" ]]; then
            tail -n 10 "$ALERT_LOG" 2>/dev/null || echo "  No recent alerts"
        else
            echo "  No alerts log found"
        fi
    } > "$report_file"

    log_info "Monitoring report saved to: $report_file"
}

# Main monitoring loop
monitoring_loop() {
    log_info "Starting ETL Plus monitoring (interval: ${MONITORING_INTERVAL}s)"

    while true; do
        check_system_resources
        check_service_health
        check_websocket_health
        check_database_health
        check_log_errors
        check_feature_flags

        generate_report

        log_info "Monitoring cycle completed. Next check in ${MONITORING_INTERVAL} seconds..."
        sleep "$MONITORING_INTERVAL"
    done
}

# Single check mode
single_check() {
    log_info "Performing single monitoring check..."

    check_system_resources
    check_service_health
    check_websocket_health
    check_database_health
    check_log_errors
    check_feature_flags

    generate_report

    log_info "Single check completed"

    # Show summary
    echo ""
    echo "Monitoring Summary:"
    echo "==================="
    for i in "${!COMPONENT_KEYS[@]}"; do
        component="${COMPONENT_KEYS[$i]}"
        status="${COMPONENT_VALUES[$i]}"
        echo "  $component: $status"
    done
}

# Show usage information
show_usage() {
    cat << EOF
ETL Plus Monitoring and Alerting Script

Usage: $0 [command] [options]

Commands:
  start              Start continuous monitoring
  check              Perform single monitoring check
  report             Generate monitoring report only
  help               Show this help message

Configuration:
  Monitoring Interval: ${MONITORING_INTERVAL} seconds
  Alert Email: $ALERT_EMAIL
  Memory Threshold: ${MEMORY_THRESHOLD}%
  CPU Threshold: ${CPU_THRESHOLD}%
  Disk Threshold: ${DISK_THRESHOLD}%

Log Files:
  Monitoring: $LOG_FILE
  Alerts: $ALERT_LOG

EOF
}

# Main execution
main() {
    local command="$1"

    case "$command" in
        "start")
            monitoring_loop
            ;;
        "check")
            single_check
            ;;
        "report")
            generate_report
            ;;
        "help"|"-h"|"--help"|"")
            show_usage
            ;;
        *)
            log_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
