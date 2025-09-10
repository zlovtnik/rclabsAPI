#!/bin/bash

# ETL Plus Backend Deployment Script
# Deploys the application using Helm charts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
HELM_CHART_DIR="$PROJECT_ROOT/helm/etlplus-backend"
LOG_FILE="$PROJECT_ROOT/logs/deploy_$(date +%Y%m%d_%H%M%S).log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
NAMESPACE="default"
RELEASE_NAME="etlplus-backend"
ENVIRONMENT="staging"
VALUES_FILE="$HELM_CHART_DIR/values.yaml"

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
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Deploy ETL Plus Backend using Helm"
    echo ""
    echo "Options:"
    echo "  -n, --namespace NAMESPACE    Kubernetes namespace (default: default)"
    echo "  -r, --release RELEASE        Helm release name (default: etlplus-backend)"
    echo "  -e, --environment ENV        Environment (staging|production) (default: staging)"
    echo "  -f, --values-file FILE       Custom values file"
    echo "  -u, --upgrade                Upgrade existing release"
    echo "  -d, --dry-run                Dry run mode"
    echo "  -h, --help                   Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 --environment production"
    echo "  $0 --upgrade --namespace production"
    echo "  $0 --dry-run --values-file custom-values.yaml"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--namespace)
            NAMESPACE="$2"
            shift 2
            ;;
        -r|--release)
            RELEASE_NAME="$2"
            shift 2
            ;;
        -e|--environment)
            ENVIRONMENT="$2"
            shift 2
            ;;
        -f|--values-file)
            VALUES_FILE="$2"
            shift 2
            ;;
        -u|--upgrade)
            UPGRADE=true
            shift
            ;;
        -d|--dry-run)
            DRY_RUN="--dry-run"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Validate inputs
if [[ ! -d "$HELM_CHART_DIR" ]]; then
    log_error "Helm chart directory not found: $HELM_CHART_DIR"
    exit 1
fi

if [[ ! -f "$VALUES_FILE" ]]; then
    log_error "Values file not found: $VALUES_FILE"
    exit 1
fi

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    if ! command -v helm &> /dev/null; then
        log_error "Helm is not installed or not in PATH"
        exit 1
    fi

    if ! command -v kubectl &> /dev/null; then
        log_error "kubectl is not installed or not in PATH"
        exit 1
    fi

    if ! kubectl cluster-info &> /dev/null; then
        log_error "Unable to connect to Kubernetes cluster"
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# Create namespace if it doesn't exist
create_namespace() {
    if ! kubectl get namespace "$NAMESPACE" &> /dev/null; then
        log_info "Creating namespace: $NAMESPACE"
        kubectl create namespace "$NAMESPACE"
        log_success "Namespace created"
    else
        log_info "Namespace already exists: $NAMESPACE"
    fi
}

# Deploy or upgrade
deploy() {
    local action="install"
    if [[ "$UPGRADE" == "true" ]] || helm status "$RELEASE_NAME" -n "$NAMESPACE" &> /dev/null; then
        action="upgrade"
    fi

    log_info "Starting Helm $action for release: $RELEASE_NAME in namespace: $NAMESPACE"

    # Set environment-specific values
    local set_values=(
        "--set" "environment=$ENVIRONMENT"
        "--set" "ingress.hosts[0].host=etlplus-$ENVIRONMENT.yourdomain.com"
    )

    if [[ -n "$DRY_RUN" ]]; then
        log_warn "DRY RUN MODE - No changes will be applied"
        set_values+=("$DRY_RUN")
    fi

    helm $action "$RELEASE_NAME" "$HELM_CHART_DIR" \
        --namespace "$NAMESPACE" \
        --values "$VALUES_FILE" \
        "${set_values[@]}"

    if [[ -z "$DRY_RUN" ]]; then
        log_info "Waiting for deployment to be ready..."
        kubectl wait --for=condition=available --timeout=300s deployment/"$RELEASE_NAME" -n "$NAMESPACE"

        log_success "Deployment completed successfully"
        log_info "Application should be available at: https://etlplus-$ENVIRONMENT.yourdomain.com"
    else
        log_success "Dry run completed"
    fi
}

# Main execution
main() {
    log_info "Starting ETL Plus Backend deployment"
    log_info "Environment: $ENVIRONMENT"
    log_info "Namespace: $NAMESPACE"
    log_info "Release: $RELEASE_NAME"
    log_info "Values file: $VALUES_FILE"

    check_prerequisites
    create_namespace
    deploy

    log_success "Deployment script completed"
}

# Run main function
main "$@"
