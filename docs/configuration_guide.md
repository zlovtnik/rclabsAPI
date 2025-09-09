# ETL Plus Configuration Guide

## Overview

The ETL Plus system features a comprehensive configuration management system that supports multiple configuration sources, environment-specific overrides, and runtime configuration updates. This guide covers all aspects of configuring the refactored ETL Plus system.

## Configuration Architecture

### Configuration Hierarchy

The configuration system follows a hierarchical approach with multiple sources:

```text
Environment Variables (Highest Priority)
    │
    ├── Runtime Overrides
    │
    ├── Environment-Specific Files
    │       │
    │       ├── config.production.json
    │       ├── config.staging.json
    │       └── config.development.json
    │
    └── Base Configuration (Lowest Priority)
            │
            └── config.json
```

### Configuration Sources

#### 1. Base Configuration File

The base configuration file contains all default settings:

```json
{
  "application": {
    "name": "ETL Plus",
    "version": "2.0.0",
    "debug": false,
    "log_level": "info"
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "workers": 4,
    "timeout": 30
  },
  "database": {
    "host": "localhost",
    "port": 5432,
    "name": "etl_plus",
    "user": "etl_user",
    "password": "secure_password",
    "pool_size": 10,
    "connection_timeout": 30
  },
  "websocket": {
    "enabled": true,
    "port": 8081,
    "max_connections": 1000,
    "heartbeat_interval": 30,
    "message_timeout": 10
  },
  "logging": {
    "handlers": ["console", "file"],
    "file": {
      "path": "/var/log/etl_plus/app.log",
      "max_size": "100MB",
      "rotation": "daily",
      "retention": 30
    },
    "async": true,
    "queue_size": 1000
  },
  "security": {
    "cors_origins": ["http://localhost:3000"],
    "rate_limiting": {
      "enabled": true,
      "requests_per_minute": 100
    },
    "authentication": {
      "enabled": true,
      "jwt_secret": "your-secret-key",
      "token_expiry": 3600
    }
  }
}
```

#### 2. Environment-Specific Configuration

Override settings for different deployment environments:

**config.production.json:**

```json
{
  "application": {
    "debug": false,
    "log_level": "warn"
  },
  "database": {
    "host": "prod-db.example.com",
    "password": "${DB_PASSWORD}"
  },
  "server": {
    "workers": 8
  },
  "logging": {
    "file": {
      "path": "/var/log/etl_plus/production.log"
    }
  }
}
```

**config.development.json:**

```json
{
  "application": {
    "debug": true,
    "log_level": "debug"
  },
  "database": {
    "host": "localhost",
    "password": "dev_password"
  },
  "logging": {
    "handlers": ["console"]
  }
}
```

#### 3. Environment Variables

Environment variables override configuration values and support secrets:

```bash
# Application settings
export ETL_PLUS_APP_DEBUG=true
export ETL_PLUS_APP_LOG_LEVEL=debug

# Database settings
export ETL_PLUS_DATABASE_HOST=prod-db.example.com
export ETL_PLUS_DATABASE_PASSWORD=secure_prod_password

# Server settings
export ETL_PLUS_SERVER_PORT=8080
export ETL_PLUS_SERVER_WORKERS=8

# Security settings
export ETL_PLUS_SECURITY_JWT_SECRET=your-production-jwt-secret
```

#### 4. Runtime Configuration

Dynamic configuration updates without restart:

```cpp
#include "config_manager.hpp"

// Update configuration at runtime
ConfigManager& config = ConfigManager::instance();
config.set("database.pool_size", 20);
config.set("websocket.max_connections", 1500);

// Watch for configuration changes
config.watch("database.host", [](const std::string& newHost) {
    logger.info("Database host changed to: " + newHost);
    // Reconnect to new database host
});
```

## Component-Specific Configuration

### Logger Configuration

#### Basic Logging Setup

```json
{
  "logging": {
    "level": "info",
    "handlers": ["console", "file"],
    "async": true,
    "queue_size": 1000
  }
}
```

#### File Handler Configuration

```json
{
  "logging": {
    "file": {
      "path": "/var/log/etl_plus/app.log",
      "max_size": "100MB",
      "rotation": "daily",
      "retention_days": 30,
      "compression": "gzip"
    }
  }
}
```

#### Component-Specific Logging

```json
{
  "logging": {
    "components": {
      "ETLJobManager": "debug",
      "ConnectionPoolManager": "info",
      "WebSocketManager": "warn"
    }
  }
}
```

### Database Configuration

#### Connection Pool Settings

```json
{
  "database": {
    "host": "localhost",
    "port": 5432,
    "name": "etl_plus",
    "user": "etl_user",
    "password": "secure_password",
    "pool": {
      "min_connections": 5,
      "max_connections": 20,
      "connection_timeout": 30,
      "idle_timeout": 300,
      "max_lifetime": 3600
    },
    "ssl": {
      "enabled": true,
      "mode": "require",
      "cert_file": "/path/to/client.crt",
      "key_file": "/path/to/client.key"
    }
  }
}
```

#### Advanced Database Options

```json
{
  "database": {
    "retry": {
      "max_attempts": 3,
      "backoff_multiplier": 2.0,
      "initial_delay": 1.0
    },
    "health_check": {
      "enabled": true,
      "interval": 30,
      "timeout": 5,
      "query": "SELECT 1"
    }
  }
}
```

### WebSocket Configuration

#### Basic WebSocket Setup

```json
{
  "websocket": {
    "enabled": true,
    "host": "0.0.0.0",
    "port": 8081,
    "max_connections": 1000,
    "connection_timeout": 30
  }
}
```

#### Advanced WebSocket Options

```json
{
  "websocket": {
    "heartbeat": {
      "enabled": true,
      "interval": 30,
      "timeout": 10
    },
    "message": {
      "max_size": "1MB",
      "timeout": 5,
      "compression": true
    },
    "security": {
      "origins": ["https://yourdomain.com"],
      "protocols": ["chat", "notification"]
    }
  }
}
```

### HTTP Server Configuration

#### Basic Server Setup

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "workers": 4,
    "backlog": 1024,
    "timeout": {
      "read": 30,
      "write": 30,
      "idle": 60
    }
  }
}
```

#### Advanced Server Options

```json
{
  "server": {
    "ssl": {
      "enabled": true,
      "certificate": "/path/to/server.crt",
      "private_key": "/path/to/server.key",
      "ciphers": "HIGH:!aNULL:!MD5"
    },
    "cors": {
      "enabled": true,
      "origins": ["https://yourdomain.com"],
      "methods": ["GET", "POST", "PUT", "DELETE"],
      "headers": ["Content-Type", "Authorization"],
      "credentials": true
    }
  }
}
```

### Security Configuration

#### Authentication Settings

```json
{
  "security": {
    "authentication": {
      "enabled": true,
      "type": "jwt",
      "jwt": {
        "secret": "your-secret-key",
        "algorithm": "HS256",
        "expiry": 3600,
        "issuer": "etl-plus",
        "audience": "etl-clients"
      }
    }
  }
}
```

#### Authorization Settings

```json
{
  "security": {
    "authorization": {
      "enabled": true,
      "policies": [
        {
          "name": "admin_only",
          "roles": ["admin"],
          "resources": ["/api/admin/*"]
        },
        {
          "name": "user_access",
          "roles": ["user", "admin"],
          "resources": ["/api/jobs/*", "/api/status"]
        }
      ]
    }
  }
}
```

#### Rate Limiting

```json
{
  "security": {
    "rate_limiting": {
      "enabled": true,
      "global": {
        "requests_per_minute": 1000,
        "burst_limit": 100
      },
      "endpoints": {
        "/api/jobs": {
          "requests_per_minute": 100,
          "burst_limit": 20
        },
        "/api/admin/*": {
          "requests_per_minute": 50,
          "burst_limit": 10
        }
      }
    }
  }
}
```

## Configuration Management

### Loading Configuration

#### Programmatic Loading

```cpp
#include "config_manager.hpp"

int main() {
    ConfigManager& config = ConfigManager::instance();

    // Load base configuration
    config.loadFromFile("config.json");

    // Load environment-specific configuration
    std::string env = getEnvironment();
    if (!env.empty()) {
        config.loadFromFile("config." + env + ".json");
    }

    // Load environment variables
    config.loadFromEnvironment();

    // Application is now configured
    startApplication();
}
```

#### Configuration Validation

```cpp
#include "config_validator.hpp"

// Validate configuration at startup
ConfigValidator validator;
ValidationResult result = validator.validate(config);

if (!result.isValid()) {
    logger.error("Configuration validation failed:");
    for (const auto& error : result.getErrors()) {
        logger.error("  " + error.field + ": " + error.message);
    }
    return EXIT_FAILURE;
}
```

### Runtime Configuration Updates

#### Dynamic Reconfiguration

```cpp
#include "config_manager.hpp"

// Update configuration dynamically
void updateDatabasePoolSize(size_t newSize) {
    ConfigManager& config = ConfigManager::instance();
    config.set("database.pool.max_connections", newSize);

    // Notify connection pool to adjust
    connectionPool.resize(newSize);
}

// Watch for configuration changes
void setupConfigurationWatchers() {
    ConfigManager& config = ConfigManager::instance();

    config.watch("database.pool.max_connections",
        [](const size_t& newSize) {
            logger.info("Database pool size changed to: " + std::to_string(newSize));
            connectionPool.resize(newSize);
        });

    config.watch("websocket.max_connections",
        [](const size_t& newMax) {
            logger.info("WebSocket max connections changed to: " + std::to_string(newMax));
            wsManager.setMaxConnections(newMax);
        });
}
```

### Configuration Hot Reloading

#### File-Based Reloading

```cpp
#include "config_reloader.hpp"

// Setup configuration file watching
ConfigReloader reloader("config.json", std::chrono::seconds(30));

reloader.setReloadCallback([]() {
    logger.info("Configuration file changed, reloading...");
    ConfigManager& config = ConfigManager::instance();

    try {
        config.reloadFromFile("config.json");
        logger.info("Configuration reloaded successfully");
    } catch (const std::exception& e) {
        logger.error("Failed to reload configuration: " + std::string(e.what()));
    }
});

reloader.start();
```

## Environment-Specific Setup

### Development Environment

**config.development.json:**

```json
{
  "application": {
    "debug": true,
    "log_level": "debug"
  },
  "database": {
    "host": "localhost",
    "password": "dev_password"
  },
  "logging": {
    "handlers": ["console"],
    "console": {
      "colors": true,
      "timestamps": true
    }
  }
}
```

### Staging Environment

**config.staging.json:**

```json
{
  "application": {
    "debug": false,
    "log_level": "info"
  },
  "database": {
    "host": "staging-db.example.com",
    "ssl": {
      "enabled": true
    }
  },
  "server": {
    "workers": 4
  }
}
```

### Production Environment

**config.production.json:**

```json
{
  "application": {
    "debug": false,
    "log_level": "warn"
  },
  "database": {
    "host": "prod-db.example.com",
    "pool": {
      "max_connections": 50
    },
    "ssl": {
      "enabled": true,
      "mode": "verify-full"
    }
  },
  "server": {
    "workers": 8,
    "ssl": {
      "enabled": true
    }
  },
  "logging": {
    "file": {
      "path": "/var/log/etl_plus/production.log",
      "max_size": "500MB"
    }
  }
}
```

## Configuration Best Practices

### Security Considerations

1. **Never commit secrets** to version control
2. **Use environment variables** for sensitive data
3. **Validate configuration** at startup
4. **Log configuration changes** for audit trails

### Performance Optimization

1. **Use connection pooling** for database connections
2. **Configure appropriate timeouts** to prevent hanging
3. **Set reasonable limits** on resource usage
4. **Enable compression** for large responses

### Monitoring and Alerting

1. **Monitor configuration changes** in production
2. **Alert on configuration validation failures**
3. **Log configuration reload events**
4. **Track configuration-driven metrics**

### Deployment Considerations

1. **Use configuration templating** for different environments
2. **Validate configuration** before deployment
3. **Document configuration options** for operations teams
4. **Plan configuration migration** strategies

## Troubleshooting

### Common Configuration Issues

#### Configuration File Not Found

```cpp
try {
    config.loadFromFile("config.json");
} catch (const ConfigurationException& e) {
    logger.error("Failed to load configuration: " + std::string(e.what()));
    // Use default configuration or exit
}
```

#### Invalid Configuration Values

```cpp
// Validate critical configuration values
if (config.get<int>("server.port") <= 0) {
    logger.error("Invalid server port configuration");
    return EXIT_FAILURE;
}
```

#### Environment Variable Issues

```cpp
// Check for required environment variables
const char* dbPassword = std::getenv("ETL_PLUS_DATABASE_PASSWORD");
if (!dbPassword) {
    logger.error("Required environment variable ETL_PLUS_DATABASE_PASSWORD not set");
    return EXIT_FAILURE;
}
```

#### Handling Configuration Update Failures

```cpp
// Handle configuration update failures gracefully
try {
    config.set("database.pool_size", newSize);
} catch (const ConfigurationException& e) {
    logger.error("Failed to update configuration: " + std::string(e.what()));
    // Continue with old configuration
}
```

This configuration guide provides comprehensive coverage of the ETL Plus configuration system, enabling proper setup and management across different deployment environments.
