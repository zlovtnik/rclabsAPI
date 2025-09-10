# Multi-stage Dockerfile for ETL Plus Backend with security hardening
FROM ubuntu:22.04 AS builder

# Install build dependencies with security updates
RUN apt-get update && apt-get upgrade -y && apt-get install -y \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    git \
    libboost-all-dev \
    libpqxx-dev \
    libspdlog-dev \
    libssl-dev \
    ninja-build \
    nlohmann-json3-dev \
    pkg-config \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && rm -rf /tmp/*

# Create non-root user for build
RUN groupadd -r builder && useradd -r -g builder builder

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Create build directory and set permissions
RUN mkdir -p build && chown -R builder:builder /app

# Build the application as non-root user
USER builder
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -GNinja && cmake --build build --parallel $(nproc)

# Runtime stage with minimal image
FROM ubuntu:22.04 AS runtime

# Install minimal runtime dependencies
RUN apt-get update && apt-get upgrade -y && apt-get install -y \
    ca-certificates \
    libboost-filesystem1.74.0 \
    libboost-system1.74.0 \
    libboost-thread1.74.0 \
    libpqxx-6.4 \
    libssl3 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && rm -rf /tmp/* \
    && rm -rf /var/tmp/* \
    && groupadd -r etlplus \
    && useradd -r -g etlplus -s /bin/false etlplus

# Set working directory
WORKDIR /app

# Copy binary and configuration from builder stage
COPY --from=builder --chown=etlplus:etlplus /app/build/bin/ETLPlusBackend /app/
COPY --from=builder --chown=etlplus:etlplus /app/config/ /app/config/

# Create logs directory with proper permissions and symlink
RUN mkdir -p /app/logs && chown -R etlplus:etlplus /app && chmod -R 755 /app && ln -s /app/config/config.json /app/config.json

# Switch to non-root user
USER etlplus

# Expose port
EXPOSE 8080

# Health check with proper user context
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f --max-time 10 http://localhost:8080/health || exit 1

# Set read-only root filesystem (except for /app/logs and /tmp)
# Note: This requires the application to write logs to /app/logs
VOLUME ["/app/logs"]

# Run the application
CMD ["/app/ETLPlusBackend"]
