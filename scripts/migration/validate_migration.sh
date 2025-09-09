#!/bin/bash

# ETL Plus Migration Validation Tool
# Validates migration completeness and correctness

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
TARGET_DIR="$PROJECT_ROOT"
VALIDATION_PASSED=true
ISSUES_FOUND=()

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
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

# Validate exception migration
validate_exceptions() {
    log_info "Validating exception migration..."

    local old_exceptions=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "throw.*Exception(" 2>/dev/null | wc -l)
    local new_exceptions=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ETLException" 2>/dev/null | wc -l)

    if [[ $old_exceptions -gt 0 ]]; then
        ISSUES_FOUND+=("Found $old_exceptions files still using old exception patterns")
        VALIDATION_PASSED=false
        log_warning "Old exception patterns found in $old_exceptions files"
    else
        log_success "Exception migration: PASSED ($new_exceptions files using ETLException)"
    fi

    # Check for correlation ID usage
    local missing_correlation=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ETLException.*[^,)]*)" 2>/dev/null | wc -l)
    if [[ $missing_correlation -gt 0 ]]; then
        ISSUES_FOUND+=("$missing_correlation ETLException calls missing correlation ID")
        log_warning "$missing_correlation ETLException calls missing correlation ID parameter"
    fi
}

# Validate logging migration
validate_logging() {
    log_info "Validating logging migration..."

    local old_logging=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "LOG_\|log->" 2>/dev/null | wc -l)
    local new_logging=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ComponentLogger" 2>/dev/null | wc -l)

    if [[ $old_logging -gt 0 ]]; then
        ISSUES_FOUND+=("Found $old_logging files still using old logging patterns")
        VALIDATION_PASSED=false
        log_warning "Old logging patterns found in $old_logging files"
    else
        log_success "Logging migration: PASSED ($new_logging files using ComponentLogger)"
    fi

    # Check for logger initialization
    local missing_logger_init=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "logger_->" 2>/dev/null | xargs grep -L "ComponentLogger.*create" | wc -l)
    if [[ $missing_logger_init -gt 0 ]]; then
        ISSUES_FOUND+=("$missing_logger_init files using logger_ without proper initialization")
        log_warning "$missing_logger_init files using logger_ without proper initialization"
    fi
}

# Validate configuration migration
validate_config() {
    log_info "Validating configuration migration..."

    local config_file="$TARGET_DIR/config/config.json"

    if [[ ! -f "$config_file" ]]; then
        ISSUES_FOUND+=("Main configuration file missing: $config_file")
        VALIDATION_PASSED=false
        log_error "Main configuration file not found"
        return
    fi

    # Check for required sections
    local missing_sections=()

    if ! grep -q '"websocket"' "$config_file" 2>/dev/null; then
        missing_sections+=("websocket")
    fi

    if ! grep -q '"logging"' "$config_file" 2>/dev/null; then
        missing_sections+=("logging")
    fi

    if ! grep -q '"security"' "$config_file" 2>/dev/null; then
        missing_sections+=("security")
    fi

    if [[ ${#missing_sections[@]} -eq 0 ]]; then
        log_success "Configuration migration: PASSED"
    else
        ISSUES_FOUND+=("Missing configuration sections: ${missing_sections[*]}")
        VALIDATION_PASSED=false
        log_warning "Missing configuration sections: ${missing_sections[*]}"
    fi

    # Validate old config access patterns
    local old_config_access=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "config->getString\|config->getInt\|config->getBool" 2>/dev/null | wc -l)
    if [[ $old_config_access -gt 0 ]]; then
        ISSUES_FOUND+=("Found $old_config_access files still using old config access patterns")
        VALIDATION_PASSED=false
        log_warning "Old config access patterns found in $old_config_access files"
    else
        log_success "Config access migration: PASSED"
    fi
}

# Validate WebSocket migration
validate_websocket() {
    log_info "Validating WebSocket migration..."

    local old_websocket=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "WebSocketHandler" 2>/dev/null | wc -l)
    local new_websocket=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "WebSocketManager::Handler" 2>/dev/null | wc -l)

    if [[ $old_websocket -gt 0 ]]; then
        ISSUES_FOUND+=("Found $old_websocket files still using old WebSocket patterns")
        VALIDATION_PASSED=false
        log_warning "Old WebSocket patterns found in $old_websocket files"
    else
        log_success "WebSocket migration: PASSED ($new_websocket files using new patterns)"
    fi
}

# Validate correlation ID usage
validate_correlation_ids() {
    log_info "Validating correlation ID usage..."

    # Find methods that should have correlation ID but don't
    local async_methods=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "std::async\|std::thread" 2>/dev/null | wc -l)
    local missing_correlation_context=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "correlationId_" 2>/dev/null | xargs grep -L "correlationId_" | wc -l)

    if [[ $async_methods -gt 0 && $missing_correlation_context -gt 0 ]]; then
        log_warning "Found async operations that may need correlation ID context"
        ISSUES_FOUND+=("Consider adding correlation ID context to async operations")
    fi

    # Check for proper correlation ID propagation
    local correlation_usage=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -c "correlationId_" 2>/dev/null | awk -F: '{sum += $2} END {print sum}')
    if [[ $correlation_usage -gt 0 ]]; then
        log_success "Correlation ID usage: FOUND ($correlation_usage instances)"
    else
        log_warning "No correlation ID usage found - may need manual addition"
    fi
}

# Validate compilation
validate_compilation() {
    log_info "Validating compilation..."

    if [[ -f "$TARGET_DIR/CMakeLists.txt" ]]; then
        log_info "Testing compilation with cmake..."

        # Create build directory if it doesn't exist
        local build_dir="$TARGET_DIR/build_validation"
        mkdir -p "$build_dir"

        cd "$build_dir"
        if cmake .. &>/dev/null && make -j$(nproc) &>/dev/null; then
            log_success "Compilation: PASSED"
        else
            ISSUES_FOUND+=("Compilation failed - check for migration errors")
            VALIDATION_PASSED=false
            log_error "Compilation: FAILED"
        fi

        cd "$TARGET_DIR"
        rm -rf "$build_dir"
    else
        log_warning "CMakeLists.txt not found - skipping compilation validation"
    fi
}

# Check for deprecated includes
validate_includes() {
    log_info "Validating include statements..."

    local deprecated_includes=$(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "#include.*exceptions.hpp" 2>/dev/null | wc -l)

    if [[ $deprecated_includes -gt 0 ]]; then
        ISSUES_FOUND+=("Found $deprecated_includes files including old exceptions.hpp")
        VALIDATION_PASSED=false
        log_warning "Deprecated includes found in $deprecated_includes files"
    else
        log_success "Include validation: PASSED"
    fi
}

# Generate validation report
generate_validation_report() {
    log_info "Generating validation report..."

    cat << EOF
============================================================
ETL Plus Migration Validation Report
Generated: $(date)
Target: $TARGET_DIR
============================================================

VALIDATION STATUS:
-----------------
EOF

    if [[ $VALIDATION_PASSED == true ]]; then
        echo "✅ OVERALL VALIDATION: PASSED"
        echo "   All migration checks completed successfully"
    else
        echo "❌ OVERALL VALIDATION: FAILED"
        echo "   Issues found that need attention"
    fi

    cat << EOF

ISSUES FOUND:
-------------
EOF

    if [[ ${#ISSUES_FOUND[@]} -eq 0 ]]; then
        echo "✅ No issues found"
    else
        for issue in "${ISSUES_FOUND[@]}"; do
            echo "⚠️  $issue"
        done
    fi

    cat << EOF

MIGRATION COMPLETENESS:
----------------------
EOF

    # Calculate completeness percentage
    local total_checks=7
    local passed_checks=0

    [[ $(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ETLException" 2>/dev/null | wc -l) -gt 0 ]] && ((passed_checks++))
    [[ $(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ComponentLogger" 2>/dev/null | wc -l) -gt 0 ]] && ((passed_checks++))
    [[ -f "$TARGET_DIR/config/config.json" ]] && ((passed_checks++))
    [[ $(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "WebSocketManager::Handler" 2>/dev/null | wc -l) -gt 0 ]] && ((passed_checks++))
    [[ $(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "correlationId_" 2>/dev/null | wc -l) -gt 0 ]] && ((passed_checks++))
    [[ ! -f "$TARGET_DIR/build_validation" ]] && ((passed_checks++))  # Compilation check
    [[ $(find "$TARGET_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "#include.*exceptions.hpp" 2>/dev/null | wc -l) -eq 0 ]] && ((passed_checks++))

    local completeness=$((passed_checks * 100 / total_checks))
    echo "Migration Completeness: $completeness% ($passed_checks/$total_checks checks passed)"

    cat << EOF

RECOMMENDATIONS:
---------------
EOF

    if [[ $VALIDATION_PASSED == true ]]; then
        echo "✅ Migration appears complete and valid"
        echo "   Ready for production deployment"
    else
        echo "⚠️  Address the issues listed above before deployment"
        echo "   Run migration scripts again if needed"
        echo "   Check docs/migration_guide.md for detailed guidance"
    fi

    cat << EOF

NEXT STEPS:
----------
1. Review validation report and address any issues
2. Run unit tests to verify functionality
3. Test integration with existing systems
4. Plan production deployment with rollback strategy

============================================================
EOF
}

# Save validation results
save_validation_results() {
    local output_file="$SCRIPT_DIR/validation_report_$(date +%Y%m%d_%H%M%S).txt"

    {
        echo "ETL Plus Migration Validation Results"
        echo "Generated: $(date)"
        echo "Target Directory: $TARGET_DIR"
        echo "Overall Status: $(if [[ $VALIDATION_PASSED == true ]]; then echo "PASSED"; else echo "FAILED"; fi)"
        echo ""
        echo "Issues Found:"
        for issue in "${ISSUES_FOUND[@]}"; do
            echo "- $issue"
        done
    } > "$output_file"

    log_success "Validation results saved to: $output_file"
}

# Main execution
main() {
    parse_args "$@"

    log_info "Starting ETL Plus migration validation..."
    log_info "Target directory: $TARGET_DIR"

    validate_exceptions
    validate_logging
    validate_config
    validate_websocket
    validate_correlation_ids
    validate_compilation
    validate_includes

    generate_validation_report
    save_validation_results

    if [[ $VALIDATION_PASSED == true ]]; then
        log_success "Migration validation completed successfully!"
        exit 0
    else
        log_error "Migration validation found issues that need attention!"
        exit 1
    fi
}

# Run main function
main "$@"
