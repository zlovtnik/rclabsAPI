#!/bin/bash

# ETL Plus Configuration Migration Tool
# Migrates old configuration files to new format

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Global variables
DRY_RUN=false
BACKUP=true
VERBOSE=false
TARGET_DIR="$PROJECT_ROOT"

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run)
                DRY_RUN=true
                BACKUP=false
                shift
                ;;
            --no-backup)
                BACKUP=false
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --target-dir)
                TARGET_DIR="$2"
                shift 2
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done
}

# Create backup of file
backup_file() {
    local file="$1"
    if [[ $BACKUP == true ]]; then
        local backup="${file}.backup.$(date +%Y%m%d_%H%M%S)"
        cp "$file" "$backup"
        [[ $VERBOSE == true ]] && log_info "Created backup: $backup"
    fi
}

# Migrate main configuration file
migrate_main_config() {
    local config_file="$TARGET_DIR/config/config.json"

    if [[ ! -f "$config_file" ]]; then
        log_warning "Main config file not found: $config_file"
        return
    fi

    log_info "Migrating main configuration file..."

    # Read current config
    local temp_file=$(mktemp)
    cp "$config_file" "$temp_file"

    # Apply migrations using jq if available
    if command -v jq &> /dev/null; then
        # Add new configuration sections
        jq '. + {
            "application": (.application // {}),
            "websocket": (.websocket // {
                "enabled": true,
                "port": 8081,
                "max_connections": 1000,
                "heartbeat_interval": 30
            }),
            "logging": (.logging // {
                "handlers": ["console", "file"],
                "async": true,
                "queue_size": 1000
            }),
            "security": (.security // {
                "cors_origins": ["http://localhost:3000"],
                "rate_limiting": {
                    "enabled": true,
                    "requests_per_minute": 100
                }
            })
        }' "$temp_file" > "${temp_file}.new"

        # Update database configuration
        jq '.database = (.database // {}) | .database.pool = (.database.pool // {
            "min_connections": 5,
            "max_connections": 20,
            "connection_timeout": 30
        })' "${temp_file}.new" > "${temp_file}.migrated"

        if [[ $DRY_RUN == false ]]; then
            backup_file "$config_file"
            mv "${temp_file}.migrated" "$config_file"
            log_success "Migrated main configuration file"
        else
            log_info "Would migrate main configuration file (dry run)"
            cat "${temp_file}.migrated"
        fi

        rm -f "${temp_file}.new" "${temp_file}.migrated"
    else
        log_warning "jq not found - manual configuration migration required"
        log_info "Please see docs/configuration_guide.md for migration instructions"
    fi

    rm -f "$temp_file"
}

# Migrate environment-specific configurations
migrate_env_configs() {
    log_info "Migrating environment-specific configurations..."

    for env in development staging production; do
        local config_file="$TARGET_DIR/config/config.${env}.json"

        if [[ -f "$config_file" ]]; then
            log_info "Migrating $env configuration..."

            if [[ $DRY_RUN == false ]]; then
                backup_file "$config_file"
                # Add environment-specific overrides
                sed -i.bak 's/"debug": false/"debug": true, "log_level": "debug"/g' "$config_file" 2>/dev/null || true
                log_success "Migrated $env configuration"
            else
                log_info "Would migrate $env configuration (dry run)"
            fi
        fi
    done
}

# Create new configuration files
create_new_configs() {
    log_info "Creating new configuration files..."

    # Create render configuration
    local render_config="$TARGET_DIR/config/config.render.json"
    if [[ ! -f "$render_config" ]]; then
        cat > "$render_config" << 'EOF'
{
  "render": {
    "enabled": true,
    "port": 3000,
    "host": "localhost",
    "api_base_url": "http://localhost:8080"
  },
  "features": {
    "real_time_monitoring": true,
    "advanced_logging": true,
    "websocket_compression": true
  }
}
EOF
        log_success "Created render configuration file"
    fi
}

# Validate configuration migration
validate_config() {
    log_info "Validating configuration migration..."

    local config_file="$TARGET_DIR/config/config.json"

    if [[ -f "$config_file" ]]; then
        # Check for required sections
        local missing_sections=()

        if ! grep -q '"websocket"' "$config_file"; then
            missing_sections+=("websocket")
        fi

        if ! grep -q '"logging"' "$config_file"; then
            missing_sections+=("logging")
        fi

        if ! grep -q '"security"' "$config_file"; then
            missing_sections+=("security")
        fi

        if [[ ${#missing_sections[@]} -eq 0 ]]; then
            log_success "Configuration validation passed"
        else
            log_warning "Missing configuration sections: ${missing_sections[*]}"
            log_info "Run migration again or add missing sections manually"
        fi
    else
        log_error "Configuration file not found: $config_file"
    fi
}

# Generate migration report
generate_config_report() {
    log_info "Generating configuration migration report..."

    cat << EOF
============================================================
Configuration Migration Report
Generated: $(date)
============================================================

MIGRATION STATUS:
----------------
EOF

    # Check each config file
    for config_file in "$TARGET_DIR/config/config.json" \
                      "$TARGET_DIR/config/config.development.json" \
                      "$TARGET_DIR/config/config.staging.json" \
                      "$TARGET_DIR/config/config.production.json"; do
        if [[ -f "$config_file" ]]; then
            echo "✅ $(basename "$config_file") - Found"
        else
            echo "❌ $(basename "$config_file") - Missing"
        fi
    done

    cat << EOF

NEW FEATURES ADDED:
------------------
• WebSocket configuration section
• Enhanced logging configuration
• Security settings (CORS, rate limiting)
• Database connection pooling
• Environment-specific overrides

VALIDATION:
----------
EOF

    validate_config

    cat << EOF

NEXT STEPS:
----------
1. Review migrated configuration files
2. Test application with new configuration
3. Update any custom configuration extensions
4. See docs/configuration_guide.md for details

============================================================
EOF
}

# Main execution
main() {
    parse_args "$@"

    log_info "Starting ETL Plus configuration migration..."
    log_info "Target directory: $TARGET_DIR"
    [[ $DRY_RUN == true ]] && log_info "Running in dry-run mode"

    migrate_main_config
    migrate_env_configs
    create_new_configs
    generate_config_report

    log_success "Configuration migration completed!"
}

# Run main function
main "$@"
