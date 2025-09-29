#!/bin/bash
# build-docker.sh - Helper script for building Docker images with proper build artifact handling
#
# Usage:
#   ./build-docker.sh [HAS_BUILD_ARTIFACTS]
#
# Arguments:
#   HAS_BUILD_ARTIFACTS  Set to 'true' if build artifacts are present, 'false' otherwise (default: false)
#
# Examples:
#   ./build-docker.sh                    # Build from source (no build artifacts)
#   ./build-docker.sh true               # Use existing build artifacts
#   ./build-docker.sh false              # Force build from source

set -e

# Default to building from source if no argument provided
HAS_BUILD_ARTIFACTS=${1:-false}

echo "🐳 Building Docker image..."
echo "📦 Build artifacts present: $HAS_BUILD_ARTIFACTS"

# Build the Docker image with the appropriate build argument
if [ "$HAS_BUILD_ARTIFACTS" = "true" ]; then
    echo "🔧 Using existing build artifacts..."
    docker build \
        --build-arg HAS_BUILD_ARTIFACTS=true \
        -f Dockerfile.render \
        -t etlplus-backend \
        .
else
    echo "🔨 Building from source (no pre-built artifacts)..."
    docker build \
        --build-arg HAS_BUILD_ARTIFACTS=false \
        -f Dockerfile.render \
        -t etlplus-backend \
        .
fi

echo "✅ Docker image built successfully!"
echo "🚀 Run with: docker run -p 8080:8080 etlplus-backend"
