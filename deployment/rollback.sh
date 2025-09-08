#!/bin/bash

# ETL Plus Deployment Rollback Script
# This script provides rollback procedures for the refactored ETL Plus system

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BACKUP_DIR="$PROJECT_ROOT/backups"
LOG_FILE="$PROJECT_ROOT/logs/rollback_$(date +%Y%m%d_%H%M%S).log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# Create backup directory if it doesn't exist
create_backup_dir() {
    if [[ ! -d "$BACKUP_DIR" ]]; then
        mkdir -p "$BACKUP_DIR"
        log_info "Created backup directory: $BACKUP_DIR"
    fi
}

# Backup current state before rollback
backup_current_state() {
    local backup_name="pre_rollback_$(date +%Y%m%d_%H%M%S)"
    local backup_path="$BACKUP_DIR/$backup_name"

    log_info "Creating backup before rollback: $backup_name"

    mkdir -p "$backup_path"

    # Backup configuration files
    if [[ -f "$PROJECT_ROOT/config/config.json" ]]; then
        cp "$PROJECT_ROOT/config/config.json" "$backup_path/config.json"
    fi

    # Backup feature flags
    if [[ -f "$PROJECT_ROOT/config/feature_flags.json" ]]; then
        cp "$PROJECT_ROOT/config/feature_flags.json" "$backup_path/feature_flags.json"
    fi

    # Backup binary if it exists
    if [[ -f "$PROJECT_ROOT/build/etlplus" ]]; then
        cp "$PROJECT_ROOT/build/etlplus" "$backup_path/etlplus"
    fi

    log_success "Backup created: $backup_path"
    echo "$backup_path"
}

# Rollback feature flags to safe state
rollback_feature_flags() {
    log_info "Rolling back feature flags to safe state..."

    cat > "$PROJECT_ROOT/config/feature_flags.json" << EOF
{
  "flags": {
    "new_logger_system": false,
    "new_exception_system": false,
    "new_request_handler": false,
    "new_websocket_manager": false,
    "new_concurrency_patterns": false,
    "new_type_system": false
  },
  "rollout_percentages": {
    "new_logger_system": 0.0,
    "new_exception_system": 0.0,
    "new_request_handler": 0.0,
    "new_websocket_manager": 0.0,
    "new_concurrency_patterns": 0.0,
    "new_type_system": 0.0
  }
}
EOF

    log_success "Feature flags rolled back to safe state"
}

# Rollback to previous binary version
rollback_binary() {
    local backup_path="$1"

    if [[ -f "$backup_path/etlplus" ]]; then
        log_info "Rolling back binary to previous version..."
        cp "$backup_path/etlplus" "$PROJECT_ROOT/build/etlplus"
        log_success "Binary rolled back successfully"
    else
        log_warn "No previous binary found in backup"
    fi
}

# Rollback configuration files
rollback_config() {
    local backup_path="$1"

    if [[ -f "$backup_path/config.json" ]]; then
        log_info "Rolling back configuration to previous version..."
        cp "$backup_path/config.json" "$PROJECT_ROOT/config/config.json"
        log_success "Configuration rolled back successfully"
    else
        log_warn "No previous configuration found in backup"
    fi
}

# Restart services
restart_services() {
    log_info "Restarting ETL Plus services..."

    # Stop current processes
    pkill -f "etlplus" || true

    # Wait a moment
    sleep 2

    # Start services (adjust path as needed)
    if [[ -f "$PROJECT_ROOT/build/etlplus" ]]; then
        nohup "$PROJECT_ROOT/build/etlplus" > "$PROJECT_ROOT/logs/etlplus.log" 2>&1 &
        log_success "ETL Plus service restarted (PID: $!)"
    else
        log_error "ETL Plus binary not found, cannot restart service"
        return 1
    fi
}

# Full system rollback
full_rollback() {
    log_info "Starting full system rollback..."

    # Create backup of current state
    local backup_path=$(backup_current_state)

    # Rollback in reverse order of deployment
    rollback_feature_flags
    rollback_binary "$backup_path"
    rollback_config "$backup_path"

    # Restart services
    if restart_services; then
        log_success "Full system rollback completed successfully"
        echo "Rollback completed. System is now in safe state."
        echo "Backup location: $backup_path"
    else
        log_error "Service restart failed after rollback"
        echo "Manual intervention may be required."
        return 1
    fi
}

# Partial rollback - disable specific features
partial_rollback() {
    local feature="$1"

    if [[ -z "$feature" ]]; then
        log_error "Feature name required for partial rollback"
        echo "Usage: $0 partial <feature_name>"
        echo "Available features: logger, exception, request_handler, websocket, concurrency, types"
        return 1
    fi

    log_info "Starting partial rollback for feature: $feature"

    # Create backup
    backup_current_state > /dev/null

    # Map feature names to flag keys
    declare -A feature_map=(
        ["logger"]="new_logger_system"
        ["exception"]="new_exception_system"
        ["request_handler"]="new_request_handler"
        ["websocket"]="new_websocket_manager"
        ["concurrency"]="new_concurrency_patterns"
        ["types"]="new_type_system"
    )

    local flag_key="${feature_map[$feature]}"
    if [[ -z "$flag_key" ]]; then
        log_error "Unknown feature: $feature"
        return 1
    fi

    # Disable the specific feature
    if [[ -f "$PROJECT_ROOT/config/feature_flags.json" ]]; then
        # Use jq if available, otherwise manual edit
        if command -v jq &> /dev/null; then
            jq --arg flag "$flag_key" '.flags[$flag] = false | .rollout_percentages[$flag] = 0.0' \
               "$PROJECT_ROOT/config/feature_flags.json" > "$PROJECT_ROOT/config/feature_flags.json.tmp" && \
            mv "$PROJECT_ROOT/config/feature_flags.json.tmp" "$PROJECT_ROOT/config/feature_flags.json"
        else
            log_warn "jq not available, manually disabling feature in config"
            sed -i.bak "s/\"$flag_key\": true/\"$flag_key\": false/g" "$PROJECT_ROOT/config/feature_flags.json"
        fi
    fi

    log_success "Feature '$feature' has been disabled"
    echo "Partial rollback completed. Feature '$feature' is now disabled."
}

# Health check after rollback
health_check() {
    log_info "Performing health check after rollback..."

    # Check if service is running
    if pgrep -f "etlplus" > /dev/null; then
        log_success "ETL Plus service is running"
    else
        log_error "ETL Plus service is not running"
        return 1
    fi

    # Check configuration file
    if [[ -f "$PROJECT_ROOT/config/config.json" ]]; then
        log_success "Configuration file exists"
    else
        log_error "Configuration file missing"
        return 1
    fi

    # Check feature flags
    if [[ -f "$PROJECT_ROOT/config/feature_flags.json" ]]; then
        log_success "Feature flags file exists"
    else
        log_error "Feature flags file missing"
        return 1
    fi

    log_success "Health check passed"
    return 0
}

# Show usage information
show_usage() {
    cat << EOF
ETL Plus Rollback Script

Usage: $0 <command> [options]

Commands:
  full              Perform full system rollback to safe state
  partial <feature> Disable specific feature (logger, exception, request_handler, websocket, concurrency, types)
  backup            Create backup of current state only
  health            Run health check on current system
  help              Show this help message

Examples:
  $0 full                    # Full rollback to safe state
  $0 partial logger          # Disable new logger system only
  $0 backup                  # Create backup without rollback
  $0 health                  # Check system health

Backup Location: $BACKUP_DIR
Log File: $LOG_FILE

EOF
}

# Main execution
main() {
    local command="$1"
    local feature="$2"

    create_backup_dir

    case "$command" in
        "full")
            if full_rollback; then
                health_check
            fi
            ;;
        "partial")
            if partial_rollback "$feature"; then
                health_check
            fi
            ;;
        "backup")
            backup_current_state
            ;;
        "health")
            health_check
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
