#!/bin/sh
# Read password from secret file
if [ -f /run/secrets/redis_password ]; then
  REDIS_PASSWORD=$(cat /run/secrets/redis_password)
  export REDIS_PASSWORD
fi

# Generate redis.conf with password
cat > /etc/redis/redis.conf << EOF
# Redis configuration for ETL Plus Backend
# Basic settings
bind 127.0.0.1 ::1
port 6379
timeout 0
tcp-keepalive 300

# Memory management
maxmemory 256mb
maxmemory-policy allkeys-lfu

# Performance
tcp-backlog 511
databases 16

# Security
requirepass $REDIS_PASSWORD

# Disable dangerous commands in production
rename-command FLUSHDB ""
rename-command FLUSHALL ""
rename-command SHUTDOWN SHUTDOWN_REDIS
EOF

# Start Redis
exec redis-server /etc/redis/redis.conf