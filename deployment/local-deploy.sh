#!/bin/bash

# ETL Plus Backend Local Development Deployment Script
# Uses Docker Compose for local development and testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LOG_FILE="$PROJECT_ROOT/logs/local_deploy_$(date +%Y%m%d_%H%M%S).log"

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

# Show usage
usage() {
    echo "ETL Plus Backend Local Development Deployment"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  up          Start all services"
    echo "  down        Stop all services"
    echo "  build       Build the application image"
    echo "  logs        Show logs from all services"
    echo "  logs-app    Show logs from ETL Plus Backend only"
    echo "  logs-db     Show logs from PostgreSQL only"
    echo "  restart     Restart all services"
    echo "  status      Show status of all services"
    echo "  test        Run health checks"
    echo "  clean       Remove containers and volumes"
    echo "  help        Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 up          # Start development environment"
    echo "  $0 logs-app    # Monitor application logs"
    echo "  $0 test        # Check if services are healthy"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed or not in PATH"
        exit 1
    fi

    if ! command -v docker-compose &> /dev/null; then
        log_error "Docker Compose is not installed or not in PATH"
        exit 1
    fi

    # Check if Docker daemon is running
    if ! docker info &> /dev/null; then
        log_error "Docker daemon is not running"
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# Start services
start_services() {
    log_info "Starting ETL Plus Backend development environment..."

    cd "$PROJECT_ROOT"

    # Create logs directory if it doesn't exist
    mkdir -p logs

    # Start services
    docker-compose up -d

    log_info "Waiting for services to be healthy..."
    sleep 10

    # Wait for PostgreSQL to be ready
    log_info "Waiting for PostgreSQL..."
    docker-compose exec -T postgres pg_isready -U etl_user -d etl_db

    # Wait for application to be ready
    log_info "Waiting for ETL Plus Backend..."
    timeout=60
    while [ $timeout -gt 0 ]; do
        if curl -f http://localhost:8080/health &> /dev/null; then
            break
        fi
        sleep 5
        timeout=$((timeout - 5))
    done

    if [ $timeout -le 0 ]; then
        log_error "Application failed to start within 60 seconds"
        local failure_log="$PROJECT_ROOT/logs/startup_failure_$(date +%Y%m%d_%H%M%S).log"
        log_info "Dumping service logs to: $failure_log"
        dump_logs "$failure_log"
        log_error "Service logs dumped for debugging. Check: $failure_log"
        exit 1
    fi

    log_success "Development environment started successfully"
    show_status
    log_info "Application is available at: http://localhost:8080"
    log_info "Health check: http://localhost:8080/health"
}

# Stop services
stop_services() {
    log_info "Stopping ETL Plus Backend development environment..."
    cd "$PROJECT_ROOT"
    docker-compose down
    log_success "Services stopped"
}

# Build application
build_app() {
    log_info "Building ETL Plus Backend image..."
    cd "$PROJECT_ROOT"
    docker-compose build etlplus-backend
    log_success "Build completed"
}

# Show logs
show_logs() {
    cd "$PROJECT_ROOT"
    docker-compose logs -f
}

show_app_logs() {
    cd "$PROJECT_ROOT"
    docker-compose logs -f etlplus-backend
}

show_db_logs() {
    cd "$PROJECT_ROOT"
    docker-compose logs -f postgres
}

# Dump logs (non-following, for programmatic use)
dump_logs() {
    local output_file="${1:-}"
    cd "$PROJECT_ROOT"
    if [ -n "$output_file" ]; then
        if ! docker_compose_cmd logs --timestamps --no-color > "$output_file" 2>&1; then
            log_warn "Failed to dump all logs to $output_file"
            return 1
        fi
        log_info "All logs dumped to: $output_file"
    else
        docker_compose_cmd logs --timestamps --no-color 2>&1 || true
    fi
}

# Docker Compose wrapper for consistent usage
docker_compose_cmd() {
    docker-compose "$@"
}

dump_app_logs() {
    local output_file="${1:-}"
    cd "$PROJECT_ROOT"

    if [ -n "$output_file" ]; then
        # Use docker-compose wrapper with timestamps and no-color, redirect to file
        if ! docker_compose_cmd logs --timestamps --no-color etlplus-backend > "$output_file" 2>&1; then
            log_warn "Failed to dump application logs to $output_file"
            return 1
        fi
        log_info "Application logs dumped to: $output_file"
    else
        # Display logs to stdout with formatting
        docker_compose_cmd logs --timestamps --no-color etlplus-backend 2>&1 || true
    fi
}

dump_db_logs() {
    local output_file="${1:-}"
    cd "$PROJECT_ROOT"
    if [ -n "$output_file" ]; then
        if ! docker_compose_cmd logs --timestamps --no-color postgres > "$output_file" 2>&1; then
            log_warn "Failed to dump database logs to $output_file"
            return 1
        fi
        log_info "Database logs dumped to: $output_file"
    else
        docker_compose_cmd logs --timestamps --no-color postgres 2>&1 || true
    fi
}

# Restart services
restart_services() {
    log_info "Restarting services..."
    cd "$PROJECT_ROOT"
    docker-compose restart
    log_success "Services restarted"
}

# Show status
show_status() {
    log_info "Service Status:"
    cd "$PROJECT_ROOT"
    docker-compose ps
}

# Run health checks
run_tests() {
    log_info "Running health checks..."

    # Check PostgreSQL
    if docker-compose exec -T postgres pg_isready -U etl_user -d etl_db &> /dev/null; then
        log_success "PostgreSQL is healthy"
    else
        log_error "PostgreSQL is not healthy"
    fi

    # Check application
    if curl -f http://localhost:8080/health &> /dev/null; then
        log_success "ETL Plus Backend is healthy"
    else
        log_error "ETL Plus Backend is not responding"
    fi

    # Check API endpoints
    if curl -f http://localhost:8080/api/health &> /dev/null; then
        log_success "API health endpoint is responding"
    else
        log_warn "API health endpoint is not responding"
    fi
}

# Clean up
cleanup() {
    log_warn "This will remove all containers and volumes. Are you sure? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        log_info "Cleaning up..."
        cd "$PROJECT_ROOT"
        docker-compose down -v --remove-orphans
        docker system prune -f
        log_success "Cleanup completed"
    else
        log_info "Cleanup cancelled"
    fi
}

# Main execution
main() {
    case "${1:-help}" in
        up)
            check_prerequisites
            start_services
            ;;
        down)
            stop_services
            ;;
        build)
            check_prerequisites
            build_app
            ;;
        logs)
            show_logs
            ;;
        logs-app)
            show_app_logs
            ;;
        logs-db)
            show_db_logs
            ;;
        restart)
            restart_services
            ;;
        status)
            show_status
            ;;
        test)
            run_tests
            ;;
        clean)
            cleanup
            ;;
        help|*)
            usage
            ;;
    esac
}

# Run main function
main "$@"
