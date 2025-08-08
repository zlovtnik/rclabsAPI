# Multi-stage Dockerfile for ETL Plus Backend
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    curl \
    git \
    libboost-all-dev \
    libnlohmann-json3-dev \
    libpqxx-dev \
    libspdlog-dev \
    libssl-dev \
    ninja-build \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Build the application
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -GNinja

RUN cmake --build build --parallel $(nproc)

# Runtime stage
FROM ubuntu:22.04 AS runtime

# Install runtime dependencies and setup user
RUN apt-get update && apt-get install -y \
    ca-certificates \
    libboost-filesystem1.74.0 \
    libboost-system1.74.0 \
    libboost-thread1.74.0 \
    libpqxx-6.4 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd -r etlplus \
    && useradd -r -g etlplus etlplus

# Set working directory
WORKDIR /app

# Copy binary and configuration
COPY --from=builder /app/build/bin/ETLPlusBackend /app/
COPY --from=builder /app/config/ /app/config/

# Create logs directory
RUN mkdir -p /app/logs && chown -R etlplus:etlplus /app

# Switch to app user
USER etlplus

# Expose port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Run the application
CMD ["./ETLPlusBackend"]
