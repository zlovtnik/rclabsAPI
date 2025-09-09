#!/bin/bash

# ETL Plus Code Migration Tool
# Migrates code patterns from old system to new refactored system

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

# Migrate exception usage
migrate_exceptions() {
    log_info "Migrating exception usage patterns..."

    find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | while read -r file; do
        local changed=false

        # Skip if file contains new exception patterns (already migrated)
        if grep -q "ETLException" "$file" 2>/dev/null; then
            [[ $VERBOSE == true ]] && log_info "Skipping already migrated file: $file"
            continue
        fi

        [[ $VERBOSE == true ]] && log_info "Processing: $file"

        # Migrate old exception throws
        if grep -q "throw.*Exception" "$file" 2>/dev/null; then
            if [[ $DRY_RUN == false ]]; then
                backup_file "$file"

                # Replace old exception types with ETLException
                sed -i.bak 's/throw DatabaseException(/throw ETLException("Database error", correlationId, /g' "$file"
                sed -i.bak 's/throw NetworkException(/throw ETLException("Network error", correlationId, /g' "$file"
                sed -i.bak 's/throw ValidationException(/throw ETLException("Validation error", correlationId, /g' "$file"

                # Add correlation ID parameter if missing
                sed -i.bak 's/throw ETLException(\([^,]*\))/throw ETLException(\1, "unknown")/g' "$file"

                changed=true
                log_success "Migrated exceptions in: $file"
            else
                log_info "Would migrate exceptions in: $file (dry run)"
            fi
        fi

        # Clean up backup files created by sed
        [[ -f "${file}.bak" ]] && rm "${file}.bak"
    done
}

# Migrate logging patterns
migrate_logging() {
    log_info "Migrating logging patterns..."

    find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | while read -r file; do
        local changed=false

        # Skip if file already uses new logging patterns
        if grep -q "ComponentLogger" "$file" 2>/dev/null; then
            [[ $VERBOSE == true ]] && log_info "Skipping already migrated file: $file"
            continue
        fi

        [[ $VERBOSE == true ]] && log_info "Processing: $file"

        # Migrate old logging macros
        if grep -q "LOG_\|log->info\|log->error\|log->debug" "$file" 2>/dev/null; then
            if [[ $DRY_RUN == false ]]; then
                backup_file "$file"

                # Add ComponentLogger include if not present
                if ! grep -q "#include.*component_logger" "$file" 2>/dev/null; then
                    sed -i.bak '1a\
#include "component_logger.hpp"
' "$file"
                fi

                # Replace old logging patterns
                sed -i.bak 's/LOG_INFO(/logger_->info(/g' "$file"
                sed -i.bak 's/LOG_ERROR(/logger_->error(/g' "$file"
                sed -i.bak 's/LOG_DEBUG(/logger_->debug(/g' "$file"
                sed -i.bak 's/log->info(/logger_->info(/g' "$file"
                sed -i.bak 's/log->error(/logger_->error(/g' "$file"
                sed -i.bak 's/log->debug(/logger_->debug(/g' "$file"

                # Add correlation ID to logging calls
                sed -i.bak 's/logger_->info(\([^,)]*\))/logger_->info(\1, correlationId_)/g' "$file"
                sed -i.bak 's/logger_->error(\([^,)]*\))/logger_->error(\1, correlationId_)/g' "$file"
                sed -i.bak 's/logger_->debug(\([^,)]*\))/logger_->debug(\1, correlationId_)/g' "$file"

                changed=true
                log_success "Migrated logging in: $file"
            else
                log_info "Would migrate logging in: $file (dry run)"
            fi
        fi

        # Clean up backup files
        [[ -f "${file}.bak" ]] && rm "${file}.bak"
    done
}

# Migrate configuration access
migrate_config_access() {
    log_info "Migrating configuration access patterns..."

    find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | while read -r file; do
        local changed=false

        # Skip if file already uses new config patterns
        if grep -q "config->get<" "$file" 2>/dev/null; then
            [[ $VERBOSE == true ]] && log_info "Skipping already migrated file: $file"
            continue
        fi

        [[ $VERBOSE == true ]] && log_info "Processing: $file"

        # Migrate old configuration access
        if grep -q "config->getString\|config->getInt\|config->getBool" "$file" 2>/dev/null; then
            if [[ $DRY_RUN == false ]]; then
                backup_file "$file"

                # Replace old config access with template-based access
                sed -i.bak 's/config->getString(/config->get<std::string>(/g' "$file"
                sed -i.bak 's/config->getInt(/config->get<int>(/g' "$file"
                sed -i.bak 's/config->getBool(/config->get<bool>(/g' "$file"

                changed=true
                log_success "Migrated config access in: $file"
            else
                log_info "Would migrate config access in: $file (dry run)"
            fi
        fi

        # Clean up backup files
        [[ -f "${file}.bak" ]] && rm "${file}.bak"
    done
}

# Migrate WebSocket patterns
migrate_websocket() {
    log_info "Migrating WebSocket patterns..."

    find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | while read -r file; do
        local changed=false

        # Skip if file already uses new WebSocket patterns
        if grep -q "WebSocketManager::Handler" "$file" 2>/dev/null; then
            [[ $VERBOSE == true ]] && log_info "Skipping already migrated file: $file"
            continue
        fi

        [[ $VERBOSE == true ]] && log_info "Processing: $file"

        # Migrate old WebSocket handler patterns
        if grep -q "WebSocketHandler" "$file" 2>/dev/null; then
            if [[ $DRY_RUN == false ]]; then
                backup_file "$file"

                # Update class inheritance
                sed -i.bak 's/: public WebSocketHandler/: public WebSocketManager::Handler/g' "$file"

                # Update method signatures
                sed -i.bak 's/onMessage(/handleMessage(/g' "$file"
                sed -i.bak 's/onConnect(/onConnection(/g' "$file"
                sed -i.bak 's/onDisconnect(/onDisconnection(/g' "$file"

                changed=true
                log_success "Migrated WebSocket patterns in: $file"
            else
                log_info "Would migrate WebSocket patterns in: $file (dry run)"
            fi
        fi

        # Clean up backup files
        [[ -f "${file}.bak" ]] && rm "${file}.bak"
    done
}

# Add correlation ID tracking
add_correlation_tracking() {
    log_info "Adding correlation ID tracking..."

    find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | while read -r file; do
        # Skip if file already has correlation ID
        if grep -q "correlationId_" "$file" 2>/dev/null; then
            [[ $VERBOSE == true ]] && log_info "Skipping file with existing correlation ID: $file"
            continue
        fi

        # Only process class files
        if grep -q "class.*{" "$file" 2>/dev/null; then
            [[ $VERBOSE == true ]] && log_info "Processing: $file"

            if [[ $DRY_RUN == false ]]; then
                backup_file "$file"

                # Add correlation ID member variable
                sed -i.bak '/class.*{/a\
private:\
    std::string correlationId_;\
\
public:' "$file"

                # Update constructors to accept correlation ID
                sed -i.bak 's/^\([[:space:]]*\)\([A-Za-z_][A-Za-z0-9_]*\)::\([A-Za-z_][A-Za-z0-9_]*\)()/\1\2::\3(const std::string\& correlationId) : correlationId_(correlationId)/g' "$file"

                log_success "Added correlation ID tracking to: $file"
            else
                log_info "Would add correlation ID tracking to: $file (dry run)"
            fi

            # Clean up backup files
            [[ -f "${file}.bak" ]] && rm "${file}.bak"
        fi
    done
}

# Generate migration report
generate_code_report() {
    log_info "Generating code migration report..."

    cat << EOF
============================================================
Code Migration Report
Generated: $(date)
============================================================

MIGRATION PATTERNS APPLIED:
--------------------------
EOF

    # Count migrated files
    local exception_files=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ETLException" 2>/dev/null | wc -l)
    local logger_files=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ComponentLogger" 2>/dev/null | wc -l)
    local config_files=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "config->get<" 2>/dev/null | wc -l)
    local websocket_files=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "WebSocketManager::Handler" 2>/dev/null | wc -l)

    echo "• Exception migration: $exception_files files"
    echo "• Logging migration: $logger_files files"
    echo "• Config access migration: $config_files files"
    echo "• WebSocket migration: $websocket_files files"

    cat << EOF

VALIDATION CHECKS:
-----------------
EOF

    # Check for remaining old patterns
    local old_exceptions=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "throw.*Exception(" 2>/dev/null | wc -l)
    local old_logging=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "LOG_\|log->" 2>/dev/null | wc -l)
    local old_config=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "config->getString\|config->getInt\|config->getBool" 2>/dev/null | wc -l)

    if [[ $old_exceptions -eq 0 && $old_logging -eq 0 && $old_config -eq 0 ]]; then
        echo "✅ All migration patterns applied successfully"
    else
        echo "⚠️  Some old patterns still remain:"
        [[ $old_exceptions -gt 0 ]] && echo "   - $old_exceptions files with old exception patterns"
        [[ $old_logging -gt 0 ]] && echo "   - $old_logging files with old logging patterns"
        [[ $old_config -gt 0 ]] && echo "   - $old_config files with old config access"
    fi

    cat << EOF

NEXT STEPS:
----------
1. Review migrated code files
2. Test compilation with migrated code
3. Run unit tests to verify functionality
4. See docs/migration_guide.md for detailed examples

============================================================
EOF
}

# Main execution
main() {
    parse_args "$@"

    log_info "Starting ETL Plus code migration..."
    log_info "Target directory: $TARGET_DIR"
    [[ $DRY_RUN == true ]] && log_info "Running in dry-run mode"

    migrate_exceptions
    migrate_logging
    migrate_config_access
    migrate_websocket
    add_correlation_tracking
    generate_code_report

    log_success "Code migration completed!"
}

# Run main function
main "$@"
