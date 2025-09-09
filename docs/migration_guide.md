# ETL Plus Migration Guide for Custom Extensions

## Overview

This guide provides comprehensive instructions for migrating custom extensions from the legacy ETL Plus system to the refactored version 2.0. The refactored system introduces significant architectural improvements while maintaining backward compatibility where possible.

## Migration Assessment

### Compatibility Matrix

| Component | Backward Compatible | Migration Required | Breaking Changes |
|-----------|-------------------|-------------------|------------------|
| Core ETL Jobs | ✅ | Minor | Exception handling |
| Custom Validators | ⚠️ | Moderate | Interface changes |
| Database Extensions | ✅ | Minor | Connection pooling |
| WebSocket Handlers | ❌ | Major | Complete rewrite |
| Logging Extensions | ⚠️ | Moderate | Component logger |
| Configuration | ✅ | Minor | New options available |

### Migration Priority

1. **High Priority** - Core business logic (ETL jobs, data transformers)
2. **Medium Priority** - Custom validators, database extensions
3. **Low Priority** - WebSocket handlers, monitoring extensions

## Core ETL Job Migration

### Legacy ETL Job Structure

```cpp
// Legacy implementation
class LegacyETLJob : public ETLJob {
public:
    void execute() override {
        try {
            // Job logic here
            processData();
        } catch (const std::exception& e) {
            throw ETLException("Job failed: " + std::string(e.what()));
        }
    }
};
```

### Refactored ETL Job Structure

```cpp
// New implementation with improved error handling
class ModernETLJob : public ETLJob {
private:
    std::shared_ptr<ComponentLogger<ModernETLJob>> logger_;
    std::string correlationId_;

public:
    ModernETLJob() : logger_(ComponentLogger<ModernETLJob>::create()) {}

    void execute(const std::string& correlationId) override {
        correlationId_ = correlationId;
        logger_->info("Starting ETL job execution", correlationId_);

        try {
            processData();
            logger_->info("ETL job completed successfully", correlationId_);
        } catch (const ETLException& e) {
            logger_->error("ETL job failed: " + std::string(e.what()), correlationId_);
            throw; // Re-throw with preserved context
        } catch (const std::exception& e) {
            logger_->error("Unexpected error in ETL job: " + std::string(e.what()), correlationId_);
            throw ETLException("Job execution failed", correlationId_, e.what());
        }
    }

protected:
    virtual void processData() = 0;
};
```

### ETL Job Migration Steps

1. **Add Component Logger**
   ```cpp
   std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
   ```

2. **Update Method Signatures**
   ```cpp
   // Old
   void execute() override;

   // New
   void execute(const std::string& correlationId) override;
   ```

3. **Add Correlation ID Tracking**
   ```cpp
   std::string correlationId_;
   correlationId_ = correlationId;
   ```

4. **Enhance Error Handling**
   ```cpp
   try {
       // Your logic
   } catch (const ETLException& e) {
       logger_->error("Operation failed: " + std::string(e.what()), correlationId_);
       throw;
   }
   ```

## Custom Validator Migration

### Legacy Validator Interface

```cpp
// Legacy validator
class LegacyValidator : public InputValidator {
public:
    ValidationResult validate(const std::string& input) override {
        if (input.empty()) {
            return ValidationResult::invalid("Input cannot be empty");
        }
        return ValidationResult::valid();
    }
};
```

### Refactored Validator Interface

```cpp
// Modern validator with enhanced features
class ModernValidator : public RequestValidator {
private:
    std::shared_ptr<ComponentLogger<ModernValidator>> logger_;

public:
    ModernValidator() : logger_(ComponentLogger<ModernValidator>::create()) {}

    ValidationResult validate(const Request& request,
                            const std::string& correlationId) override {
        logger_->debug("Validating request", correlationId);

        // Enhanced validation logic
        auto result = validateRequest(request);
        if (!result.isValid()) {
            logger_->warn("Request validation failed: " + result.getMessage(), correlationId);
        }

        return result;
    }

private:
    ValidationResult validateRequest(const Request& request) {
        // Your validation logic here
        return ValidationResult::valid();
    }
};
```

### Validator Migration Steps

1. **Update Base Class**
   ```cpp
   // Old: InputValidator
   // New: RequestValidator
   ```

2. **Add Component Logger**
   ```cpp
   std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
   ```

3. **Update Method Signatures**
   ```cpp
   // Old
   ValidationResult validate(const std::string& input)

   // New
   ValidationResult validate(const Request& request, const std::string& correlationId)
   ```

4. **Add Logging**
   ```cpp
   logger_->debug("Validating request", correlationId);
   ```

## Database Extension Migration

### Legacy Database Extension

```cpp
// Legacy database extension
class LegacyDatabaseExtension {
public:
    void connect() {
        // Direct connection logic
        connection_ = std::make_unique<DatabaseConnection>();
        connection_->connect(connectionString_);
    }

    void executeQuery(const std::string& query) {
        connection_->execute(query);
    }
};
```

### Refactored Database Extension

```cpp
// Modern database extension with connection pooling
class ModernDatabaseExtension {
private:
    std::shared_ptr<ConnectionPoolManager> poolManager_;
    std::shared_ptr<ComponentLogger<ModernDatabaseExtension>> logger_;

public:
    ModernDatabaseExtension(std::shared_ptr<ConnectionPoolManager> poolManager)
        : poolManager_(poolManager),
          logger_(ComponentLogger<ModernDatabaseExtension>::create()) {}

    PooledSessionPtr getSession(const std::string& correlationId) {
        logger_->debug("Acquiring database session", correlationId);
        return poolManager_->acquireSession(correlationId);
    }

    void executeQuery(PooledSessionPtr session,
                     const std::string& query,
                     const std::string& correlationId) {
        logger_->debug("Executing query: " + query.substr(0, 100) + "...", correlationId);

        try {
            session->execute(query);
            logger_->debug("Query executed successfully", correlationId);
        } catch (const DatabaseException& e) {
            logger_->error("Query execution failed: " + std::string(e.what()), correlationId);
            throw ETLException("Database operation failed", correlationId, e.what());
        }
    }
};
```

### Database Extension Migration Steps

1. **Replace Direct Connections with Connection Pooling**
   ```cpp
   // Old: Direct connection
   connection_->connect(connectionString_);

   // New: Use connection pool
   auto session = poolManager_->acquireSession(correlationId);
   ```

2. **Add Component Logger**
   ```cpp
   std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
   ```

3. **Update Exception Handling**
   ```cpp
   // Old: Generic exceptions
   // New: ETLException with correlation ID
   throw ETLException("Database operation failed", correlationId, e.what());
   ```

4. **Add Logging for Operations**
   ```cpp
   logger_->debug("Executing query", correlationId);
   ```

## WebSocket Handler Migration

### Legacy WebSocket Handler

```cpp
// Legacy WebSocket handler
class LegacyWebSocketHandler : public WebSocketHandler {
public:
    void onMessage(const std::string& message) override {
        // Handle message
        processMessage(message);
    }

    void onConnect(WebSocketConnection* conn) override {
        connections_.push_back(conn);
    }
};
```

### Refactored WebSocket Handler

```cpp
// Modern WebSocket handler with improved architecture
class ModernWebSocketHandler : public WebSocketManager::Handler {
private:
    std::shared_ptr<ComponentLogger<ModernWebSocketHandler>> logger_;
    std::shared_ptr<WebSocketManager> wsManager_;

public:
    ModernWebSocketHandler(std::shared_ptr<WebSocketManager> wsManager)
        : wsManager_(wsManager),
          logger_(ComponentLogger<ModernWebSocketHandler>::create()) {}

    void onConnection(const WebSocketConnectionPtr& conn,
                     const std::string& correlationId) override {
        logger_->info("New WebSocket connection established", correlationId);

        // Register message handler
        conn->setMessageHandler([this, correlationId](const std::string& message) {
            handleMessage(message, correlationId);
        });
    }

    void onDisconnection(const WebSocketConnectionPtr& conn,
                        const std::string& correlationId) override {
        logger_->info("WebSocket connection closed", correlationId);
    }

private:
    void handleMessage(const std::string& message, const std::string& correlationId) {
        logger_->debug("Processing WebSocket message", correlationId);

        try {
            auto response = processMessage(message);
            wsManager_->broadcast(response, correlationId);
        } catch (const std::exception& e) {
            logger_->error("Failed to process WebSocket message: " + std::string(e.what()), correlationId);
        }
    }
};
```

### WebSocket Handler Migration Steps

1. **Update Base Class**
   ```cpp
   // Old: WebSocketHandler
   // New: WebSocketManager::Handler
   ```

2. **Use Smart Pointers**
   ```cpp
   // Old: Raw pointers
   WebSocketConnection* conn

   // New: Smart pointers
   const WebSocketConnectionPtr& conn
   ```

3. **Add Component Logger**
   ```cpp
   std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
   ```

4. **Implement New Handler Methods**
   ```cpp
   void onConnection(const WebSocketConnectionPtr& conn, const std::string& correlationId)
   void onDisconnection(const WebSocketConnectionPtr& conn, const std::string& correlationId)
   ```

## Logging Extension Migration

### Legacy Logging Extension

```cpp
// Legacy logging
class LegacyLogger {
public:
    void log(const std::string& message, LogLevel level) {
        std::cout << "[" << level << "] " << message << std::endl;
    }
};
```

### Refactored Logging Extension

```cpp
// Modern logging with component context
class ModernLogger : public LogHandler {
private:
    std::shared_ptr<ComponentLogger<ModernLogger>> logger_;

public:
    ModernLogger() : logger_(ComponentLogger<ModernLogger>::create()) {}

    void log(LogLevel level, const std::string& message,
             const std::string& component, const std::string& correlationId) override {
        // Use component logger for consistent logging
        switch (level) {
            case LogLevel::DEBUG:
                logger_->debug(message, correlationId);
                break;
            case LogLevel::INFO:
                logger_->info(message, correlationId);
                break;
            case LogLevel::WARN:
                logger_->warn(message, correlationId);
                break;
            case LogLevel::ERROR:
                logger_->error(message, correlationId);
                break;
        }
    }
};
```

### Logging Extension Migration Steps

1. **Implement LogHandler Interface**
   ```cpp
   class ModernLogger : public LogHandler
   ```

2. **Add Component Logger**
   ```cpp
   std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
   ```

3. **Update Method Signatures**
   ```cpp
   // Old
   void log(const std::string& message, LogLevel level)

   // New
   void log(LogLevel level, const std::string& message,
            const std::string& component, const std::string& correlationId)
   ```

4. **Use Component Logger Methods**
   ```cpp
   logger_->info(message, correlationId);
   ```

## Configuration Migration

### Legacy Configuration

```cpp
// Legacy configuration access
class LegacyComponent {
public:
    void initialize() {
        auto config = ConfigManager::getInstance();
        host_ = config->getString("database.host");
        port_ = config->getInt("database.port");
    }
};
```

### Refactored Configuration

```cpp
// Modern configuration with validation
class ModernComponent {
private:
    std::shared_ptr<ComponentLogger<ModernComponent>> logger_;

public:
    ModernComponent() : logger_(ComponentLogger<ModernComponent>::create()) {}

    void initialize(const std::string& correlationId) {
        logger_->info("Initializing component", correlationId);

        try {
            auto config = ConfigManager::instance();

            // Validate required configuration
            host_ = config->get<std::string>("database.host");
            if (host_.empty()) {
                throw ConfigurationException("Database host not configured");
            }

            port_ = config->get<int>("database.port", 5432); // Default value

            logger_->info("Component initialized successfully", correlationId);
        } catch (const ConfigurationException& e) {
            logger_->error("Failed to initialize component: " + std::string(e.what()), correlationId);
            throw ETLException("Component initialization failed", correlationId, e.what());
        }
    }
};
```

### Configuration Migration Steps

1. **Add Component Logger**
   ```cpp
   std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
   ```

2. **Update Method Signatures**
   ```cpp
   // Old
   void initialize()

   // New
   void initialize(const std::string& correlationId)
   ```

3. **Add Configuration Validation**
   ```cpp
   if (host_.empty()) {
       throw ConfigurationException("Database host not configured");
   }
   ```

4. **Use Template-based Access**
   ```cpp
   // Old
   host_ = config->getString("database.host");

   // New
   host_ = config->get<std::string>("database.host");
   ```

## Testing Migration

### Legacy Test Structure

```cpp
// Legacy test
TEST(LegacyComponentTest, BasicFunctionality) {
    LegacyComponent component;
    component.initialize();

    EXPECT_TRUE(component.isReady());
}
```

### Refactored Test Structure

```cpp
// Modern test with proper setup
TEST(ModernComponentTest, BasicFunctionality) {
    // Setup
    auto config = std::make_shared<MockConfigManager>();
    config->set("database.host", "localhost");
    config->set("database.port", 5432);

    // Create component with dependencies
    ModernComponent component;
    component.setConfigManager(config);

    // Test
    std::string correlationId = "test-123";
    component.initialize(correlationId);

    EXPECT_TRUE(component.isReady());
}

TEST(ModernComponentTest, ConfigurationValidation) {
    // Setup with invalid configuration
    auto config = std::make_shared<MockConfigManager>();
    config->set("database.host", ""); // Invalid

    ModernComponent component;
    component.setConfigManager(config);

    // Test
    std::string correlationId = "test-456";
    EXPECT_THROW(component.initialize(correlationId), ETLException);
}
```

### Testing Migration Steps

1. **Add Dependency Injection**
   ```cpp
   component.setConfigManager(config);
   ```

2. **Use Correlation IDs in Tests**
   ```cpp
   std::string correlationId = "test-123";
   component.initialize(correlationId);
   ```

3. **Add Configuration Validation Tests**
   ```cpp
   EXPECT_THROW(component.initialize(correlationId), ETLException);
   ```

4. **Mock Dependencies**
   ```cpp
   auto config = std::make_shared<MockConfigManager>();
   ```

## Deployment Migration

### Legacy Deployment

```bash
# Legacy startup script
#!/bin/bash
./etl_plus --config config.json --log-level info
```

### Refactored Deployment

```bash
# Modern startup script with environment support
#!/bin/bash

# Set environment
export ETL_PLUS_ENV=production
export ETL_PLUS_DATABASE_PASSWORD=$(aws secretsmanager get-secret-value --secret-id etl-plus-db-password --query SecretString --output text)

# Start application
./etl_plus
```

### Deployment Migration Steps

1. **Use Environment Variables**
   ```bash
   export ETL_PLUS_ENV=production
   ```

2. **Externalize Secrets**
   ```bash
   export ETL_PLUS_DATABASE_PASSWORD=$(aws secretsmanager get-secret-value ...)
   ```

3. **Remove Command Line Config**
   ```bash
   # Old: --config config.json
   # New: Configuration loaded automatically
   ```

4. **Add Health Checks**
   ```bash
   # Add health check endpoint
   curl http://localhost:8080/health
   ```

## Migration Checklist

### Pre-Migration
- [ ] Backup existing codebase
- [ ] Review custom extensions inventory
- [ ] Identify high-priority components
- [ ] Plan rollback strategy
- [ ] Setup development environment

### Migration Execution
- [ ] Update core ETL jobs
- [ ] Migrate custom validators
- [ ] Update database extensions
- [ ] Rewrite WebSocket handlers
- [ ] Update logging extensions
- [ ] Migrate configuration access
- [ ] Update tests
- [ ] Update deployment scripts

### Post-Migration
- [ ] Run full test suite
- [ ] Performance testing
- [ ] Load testing
- [ ] Monitor error rates
- [ ] Update documentation
- [ ] Train development team

## Common Migration Issues

### Issue: Compilation Errors with Correlation ID

**Problem:** Methods updated to require correlation ID parameter
**Solution:**
```cpp
// Old
component->process();

// New
std::string correlationId = generateCorrelationId();
component->process(correlationId);
```

### Issue: Logger Not Available

**Problem:** Component logger not initialized
**Solution:**
```cpp
// Add to class definition
std::shared_ptr<ComponentLogger<T>> logger_ = ComponentLogger<T>::create();
```

### Issue: Configuration Access Changes

**Problem:** Template-based configuration access
**Solution:**
```cpp
// Old
int port = config->getInt("server.port");

// New
int port = config->get<int>("server.port");
```

### Issue: Exception Handling Updates

**Problem:** ETLException now requires correlation ID
**Solution:**
```cpp
// Old
throw ETLException("Operation failed");

// New
throw ETLException("Operation failed", correlationId, details);
```

## Support and Resources

### Getting Help

1. **Documentation**
   - System Architecture Guide (`docs/architecture/system_architecture.md`)
   - API Documentation (`docs/api/overview.md`)
   - Configuration Guide (`docs/configuration_guide.md`)

2. **Community Resources**
   - GitHub Issues for migration questions
   - Stack Overflow with `etl-plus` tag
   - Developer forum discussions

3. **Professional Services**
   - Migration assessment and planning
   - Custom extension development
   - Performance optimization
   - Training and knowledge transfer

### Best Practices

1. **Incremental Migration**
   - Migrate components in priority order
   - Test each component thoroughly
   - Maintain backward compatibility where possible

2. **Quality Assurance**
   - Comprehensive test coverage
   - Performance benchmarking
   - Load testing with production data

3. **Team Preparation**
   - Training on new architecture
   - Code review guidelines
   - Documentation updates

This migration guide provides a comprehensive roadmap for updating custom extensions to work with the refactored ETL Plus system. The new architecture offers improved reliability, performance, and maintainability while requiring careful migration planning and execution.
