#!/bin/bash

# ETL Plus GitHub Issues Creation Script
# This script creates GitHub issues based on the roadmap and documentation
# Usage: ./create_github_issues.sh

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    print_error "GitHub CLI (gh) is not installed. Please install it first:"
    echo "  brew install gh"
    echo "  # or visit: https://cli.github.com/"
    exit 1
fi

# Check if authenticated with GitHub
if ! gh auth status &> /dev/null; then
    print_error "Not authenticated with GitHub. Please run:"
    echo "  gh auth login"
    exit 1
fi

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "Not in a git repository. Please run this script from the project root."
    exit 1
fi

# Get the repository name from git remote
REPO_URL=$(git config --get remote.origin.url)
if [[ -z "$REPO_URL" ]]; then
    print_error "No git remote origin found. Please ensure you're in the correct repository."
    exit 1
fi

# Extract repository name from URL
if [[ "$REPO_URL" =~ github\.com[:/]([^/]+/[^/]+)(\.git)?$ ]]; then
    REPO_NAME="${BASH_REMATCH[1]}"
    REPO_NAME="${REPO_NAME%.git}"
else
    print_error "Could not parse repository name from: $REPO_URL"
    exit 1
fi

print_status "Repository detected: $REPO_NAME"
print_status "Creating labels first..."

# Create labels if they don't exist
declare -a LABELS=(
    "priority-high:#d73a4a:High priority items"
    "priority-medium:#fbca04:Medium priority items"
    "priority-low:#0e8a16:Low priority items"
    "phase-2:#1d76db:Phase 2 - Core Stability"
    "phase-3:#5319e7:Phase 3 - Feature Enhancement"
    "phase-4:#f9d71c:Phase 4 - Production Features"
    "phase-5:#d4c5f9:Phase 5 - Deployment & Scaling"
    "bug:#d73a4a:Something isn't working"
    "enhancement:#a2eeef:New feature or request"
    "documentation:#0075ca:Improvements or additions to documentation"
    "testing:#1d76db:Testing related issues"
    "security:#b60205:Security related issues"
    "database:#0052cc:Database related issues"
    "etl:#006b75:ETL pipeline related issues"
    "authentication:#7057ff:Authentication related issues"
    "monitoring:#d4c5f9:Monitoring and observability"
    "frontend:#bfd4f2:Frontend development"
    "deployment:#0e8a16:Deployment and infrastructure"
    "performance:#fbca04:Performance improvements"
    "configuration:#c2e0c6:Configuration management"
    "architecture:#e4e669:Architecture improvements"
    "technical-debt:#fef2c0:Technical debt items"
)

for label_info in "${LABELS[@]}"; do
    # Split by colon but only on the first 3 occurrences
    name=$(echo "$label_info" | cut -d':' -f1)
    color=$(echo "$label_info" | cut -d':' -f2)
    description=$(echo "$label_info" | cut -d':' -f3-)
    
    # Check if label exists, if not create it
    if ! gh label list --repo "$REPO_NAME" | grep -q "^$name"; then
        gh label create "$name" --color "$color" --description "$description" --repo "$REPO_NAME" 2>/dev/null || true
        print_status "Created label: $name"
    fi
done

print_status "Creating GitHub issues for ETL Plus project..."

# Phase 2: Core Stability & Bug Fixes
print_status "Creating Phase 2 issues (Core Stability & Bug Fixes)..."

gh issue create \
    --repo "$REPO_NAME" \
    --title "🐛 Memory management review and optimization" \
    --body "## Description
Review and optimize memory management throughout the application.

## Tasks
- [ ] Memory leak detection and fixes
- [ ] Proper shared_ptr lifecycle management
- [ ] Request size limits validation
- [ ] Session cleanup on errors
- [ ] Memory profiling with valgrind

## Priority
High - Critical for stability

## Acceptance Criteria
- [ ] No memory leaks detected in testing
- [ ] Proper session cleanup implemented
- [ ] Memory usage optimized
- [ ] Valgrind clean runs

## Related Files
- \`src/http_server.cpp\`
- \`src/request_handler.cpp\`
- All source files with pointer operations" \
    --label "bug,priority-high,phase-2" \
    --assignee "@me"

gh issue create \
    --repo "$REPO_NAME" \
    --title "🔌 Oracle C++ Database Integration" \
    --body "## Description
Integrate Oracle C++ libraries (OCCI or SOCI) to replace simulated database operations.

## Tasks
- [ ] Choose between OCCI and SOCI libraries
- [ ] Integrate Oracle C++ libraries
- [ ] Implement connection pooling
- [ ] Add database schema migration scripts
- [ ] Transaction management improvements
- [ ] Database health checks

## Priority
High - Required for production

## Acceptance Criteria
- [ ] Real Oracle database connections working
- [ ] Connection pooling implemented
- [ ] Migration scripts created
- [ ] Health checks functional

## Related Files
- \`src/database_manager.cpp\`
- \`include/database_manager.hpp\`
- \`CMakeLists.txt\`" \
    --label "enhancement,priority-high,phase-2,database" \
    --assignee "@me"

gh issue create \
    --repo "$REPO_NAME" \
    --title "🛡️ Enhanced Error Handling and Input Validation" \
    --body "## Description
Implement comprehensive error handling and input validation for all endpoints.

## Tasks
- [ ] Structured error responses
- [ ] Input validation for all endpoints
- [ ] Proper exception handling
- [ ] Logging system implementation
- [ ] Request/response middleware

## Priority
High - Security and stability

## Acceptance Criteria
- [ ] All endpoints have input validation
- [ ] Structured error response format
- [ ] Comprehensive logging implemented
- [ ] Exception handling covers all scenarios

## Related Files
- \`src/request_handler.cpp\`
- \`src/http_server.cpp\`
- \`include/logger.hpp\`" \
    --label "enhancement,priority-high,phase-2,security" \
    --assignee "@me"

gh issue create \
    --repo "$REPO_NAME" \
    --title "🚀 HTTP Server Performance Optimization" \
    --body "## Description
Optimize HTTP server performance with connection pooling and timeout handling.

## Tasks
- [ ] Implement connection pooling
- [ ] Add request timeout handling
- [ ] Request handling optimization
- [ ] Load testing and performance validation

## Priority
Medium - Performance improvement

## Acceptance Criteria
- [ ] Connection pooling working
- [ ] Request timeouts implemented
- [ ] Performance benchmarks improved
- [ ] Load testing passes

## Related Files
- \`src/http_server.cpp\`
- \`include/http_server.hpp\`" \
    --label "enhancement,priority-medium,phase-2,performance" \
    --assignee "@me"

print_status "✅ Phase 2 issues created successfully!"
print_status "📊 Summary:"
echo "  - Phase 2 (Core Stability): 4 issues created"

print_status "🏷️  Labels created and used:"
echo "  - Priority: priority:high, priority:medium, priority:low"
echo "  - Phase: phase:2, phase:3, phase:4, phase:5"
echo "  - Type: bug, enhancement, documentation, testing"
echo "  - Category: security, database, etl, authentication, monitoring, frontend, deployment, performance, configuration, architecture, technical-debt"

print_warning "This is a simplified version creating only Phase 2 issues."
print_warning "You can extend this script to create the remaining phases."

print_status "Next steps:"
echo "  1. Review created issues in GitHub"
echo "  2. Adjust priorities and assignments as needed"
echo "  3. Create project boards for better organization"
echo "  4. Set up milestones for each phase"
echo "  5. Begin development following the roadmap"
