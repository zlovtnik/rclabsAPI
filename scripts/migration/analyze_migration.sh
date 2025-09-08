#!/bin/bash

# ETL Plus Migration Analysis Tool
# Analyzes codebase for migration opportunities and deprecated patterns

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

# Analysis results (using indexed arrays instead of associative arrays for compatibility)
ANALYSIS_KEYS=("old_exceptions" "old_macros" "old_config" "old_websocket" "old_handler" "config_files" "old_config_format" "custom_components" "custom_validators")
ANALYSIS_VALUES=(0 0 0 0 0 0 0 0 0)

# Set analysis result
set_analysis_result() {
    local key="$1"
    local value="$2"
    for i in "${!ANALYSIS_KEYS[@]}"; do
        if [[ "${ANALYSIS_KEYS[$i]}" == "$key" ]]; then
            ANALYSIS_VALUES[$i]="$value"
            break
        fi
    done
}

# Get analysis result
get_analysis_result() {
    local key="$1"
    for i in "${!ANALYSIS_KEYS[@]}"; do
        if [[ "${ANALYSIS_KEYS[$i]}" == "$key" ]]; then
            echo "${ANALYSIS_VALUES[$i]}"
            return
        fi
    done
    echo "0"
}

# Analyze deprecated patterns in the codebase
analyze_deprecated_patterns() {
    log_info "Analyzing deprecated patterns..."

    # Find old exception handling patterns
    local old_exceptions=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "throw.*Exception\|catch.*Exception" 2>/dev/null | wc -l)
    set_analysis_result "old_exceptions" "$old_exceptions"

    # Find old macro usage
    local old_macros=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "#define.*ETL_\|ETL_.*MACRO" 2>/dev/null | wc -l)
    set_analysis_result "old_macros" "$old_macros"

    # Find old configuration access patterns
    local old_config=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "ConfigManager::getInstance\|config\[" 2>/dev/null | wc -l)
    set_analysis_result "old_config" "$old_config"

    # Find old websocket API usage
    local old_websocket=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "WebSocketManager::\|websocket_" 2>/dev/null | wc -l)
    set_analysis_result "old_websocket" "$old_websocket"

    # Find old handler patterns
    local old_handler=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "RequestHandler::\|handleRequest" 2>/dev/null | wc -l)
    set_analysis_result "old_handler" "$old_handler"
}

# Analyze configuration files
analyze_config_files() {
    log_info "Analyzing configuration files..."

    local config_files=$(find "$PROJECT_ROOT" -name "config*.json" -o -name "*.config" | wc -l)
    set_analysis_result "config_files" "$config_files"

    # Check for old config format
    local old_config_format=0
    if [[ -f "$PROJECT_ROOT/config/config.json" ]]; then
        if grep -q '"database":' "$PROJECT_ROOT/config/config.json" 2>/dev/null; then
            old_config_format=1
        fi
    fi
    set_analysis_result "old_config_format" "$old_config_format"
}

# Analyze custom extensions
analyze_custom_extensions() {
    log_info "Analyzing custom extensions..."

    # Find custom components
    local custom_components=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "class.*Component\|class.*Extension" 2>/dev/null | wc -l)
    set_analysis_result "custom_components" "$custom_components"

    # Find custom validators
    local custom_validators=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "InputValidator\|RequestValidator" 2>/dev/null | wc -l)
    set_analysis_result "custom_validators" "$custom_validators"
}

# Generate migration report
generate_report() {
    log_info "Generating migration analysis report..."

    cat << EOF
============================================================
ETL Plus Migration Analysis Report
Generated: $(date)
============================================================

DEPRECATED PATTERNS:
-------------------
• Old exception classes: $(get_analysis_result "old_exceptions") files
• Old logging macros: $(get_analysis_result "old_macros") files
• Old configuration access: $(get_analysis_result "old_config") files
• Old WebSocket patterns: $(get_analysis_result "old_websocket") files
• Old request handler patterns: $(get_analysis_result "old_handler") files

CONFIGURATION FILES:
------------------
• Configuration files found: $(get_analysis_result "config_files")
• Old config format detected: $(if [[ $(get_analysis_result "old_config_format") -eq 1 ]]; then echo "YES"; else echo "NO"; fi)

CUSTOM EXTENSIONS:
-----------------
• Custom components: $(get_analysis_result "custom_components") files
• Custom validators: $(get_analysis_result "custom_validators") files

MIGRATION PRIORITY:
------------------
EOF

    # Calculate migration complexity
    local total_files=$(( $(get_analysis_result "old_exceptions") + $(get_analysis_result "old_macros") + $(get_analysis_result "old_config") + $(get_analysis_result "old_websocket") + $(get_analysis_result "old_handler") ))

    if [[ $total_files -eq 0 ]]; then
        echo "✅ No migration needed - codebase is up to date!"
    elif [[ $total_files -lt 5 ]]; then
        echo "🟢 Low migration complexity - quick updates needed"
    elif [[ $total_files -lt 15 ]]; then
        echo "🟡 Medium migration complexity - moderate effort required"
    else
        echo "🔴 High migration complexity - significant effort required"
    fi

    cat << EOF

RECOMMENDED ACTIONS:
------------------
1. Run configuration migration: ./migrate.sh config
2. Run code pattern migration: ./migrate.sh code
3. Install compatibility layer: ./migrate.sh compatibility
4. Validate migration: ./migrate.sh validate

For detailed migration guide, see: docs/migration_guide.md
============================================================
EOF
}

# Save analysis results to file
save_results() {
    local output_file="$SCRIPT_DIR/migration_analysis_$(date +%Y%m%d_%H%M%S).txt"
    log_info "Saving analysis results to: $output_file"

    {
        echo "ETL Plus Migration Analysis Results"
        echo "Generated: $(date)"
        echo ""
        for i in "${!ANALYSIS_KEYS[@]}"; do
            echo "${ANALYSIS_KEYS[$i]}=${ANALYSIS_VALUES[$i]}"
        done
    } > "$output_file"

    log_success "Analysis results saved to $output_file"
}

# Main execution
main() {
    log_info "Starting ETL Plus migration analysis..."

    analyze_deprecated_patterns
    analyze_config_files
    analyze_custom_extensions

    generate_report
    save_results

    log_success "Migration analysis completed!"
}

# Run main function
main "$@"
