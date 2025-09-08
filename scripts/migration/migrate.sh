#!/bin/bash

# ETL Plus Migration Tools
# Main migration orchestrator script

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

# Check if we're in the right directory
check_project_root() {
    if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        log_error "Not in ETL Plus project root. Please run from project root or scripts/migration/"
        exit 1
    fi
}

# Display usage information
usage() {
    echo "ETL Plus Migration Tools"
    echo ""
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  analyze          Analyze codebase for migration opportunities"
    echo "  config           Migrate configuration files"
    echo "  code             Migrate code patterns"
    echo "  validate         Validate migration completeness"
    echo "  compatibility    Install compatibility layer"
    echo "  all              Run all migration steps"
    echo "  help             Show this help message"
    echo ""
    echo "Options:"
    echo "  --dry-run        Show what would be changed without making changes"
    echo "  --backup          Create backups before making changes"
    echo "  --verbose         Enable verbose output"
    echo "  --target-dir DIR  Target directory for migration (default: current)"
    echo ""
    echo "Examples:"
    echo "  $0 analyze"
    echo "  $0 config --dry-run"
    echo "  $0 code --backup --verbose"
    echo "  $0 all --target-dir /path/to/project"
}

# Parse command line arguments
DRY_RUN=false
BACKUP=false
VERBOSE=false
TARGET_DIR="$PROJECT_ROOT"

while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --backup)
            BACKUP=true
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
        analyze|config|code|validate|compatibility|all|help)
            COMMAND="$1"
            shift
            break
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Set default command
COMMAND="${COMMAND:-help}"

# Execute commands
case $COMMAND in
    analyze)
        log_info "Analyzing codebase for migration opportunities..."
        bash "$SCRIPT_DIR/analyze_migration.sh" "$@"
        ;;
    config)
        log_info "Migrating configuration files..."
        bash "$SCRIPT_DIR/migrate_config.sh" "$@"
        ;;
    code)
        log_info "Migrating code patterns..."
        bash "$SCRIPT_DIR/migrate_code.sh" "$@"
        ;;
    validate)
        log_info "Validating migration completeness..."
        bash "$SCRIPT_DIR/validate_migration.sh" "$@"
        ;;
    compatibility)
        log_info "Installing compatibility layer..."
        bash "$SCRIPT_DIR/install_compatibility.sh" "$@"
        ;;
    all)
        log_info "Running complete migration process..."
        bash "$SCRIPT_DIR/analyze_migration.sh" "$@"
        bash "$SCRIPT_DIR/migrate_config.sh" "$@"
        bash "$SCRIPT_DIR/migrate_code.sh" "$@"
        bash "$SCRIPT_DIR/validate_migration.sh" "$@"
        bash "$SCRIPT_DIR/install_compatibility.sh" "$@"
        log_success "Migration process completed!"
        ;;
    help|*)
        usage
        ;;
esac
