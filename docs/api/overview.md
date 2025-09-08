# ETL Plus API Documentation

## Overview

The ETL Plus API has been refactored to provide a clean, consistent, and type-safe interface for all system components. This document provides comprehensive API documentation for the new interfaces and their usage patterns.

## API Design Principles

### 1. Type Safety

- **Template-Based APIs**: Compile-time type checking and safety
- **Strong Typing**: Prevention of runtime type errors
- **Type Traits**: Compile-time introspection and validation

### 2. Fluent Interfaces

- **Method Chaining**: Readable and concise API usage
- **Builder Pattern**: Complex object construction
- **Immutable Operations**: Thread-safe operations

### 3. Error Handling

- **Exception-Based**: Clear error propagation and handling
- **Context Preservation**: Rich error context with correlation IDs
- **Recovery Support**: Graceful error recovery mechanisms

### 4. Resource Management

- **RAII Pattern**: Automatic resource cleanup
- **Smart Pointers**: Memory safety and leak prevention
- **Pool Management**: Efficient resource reuse

## Core API Components

### Logger API

#### ComponentLogger&lt;T&gt;

The ComponentLogger provides type-safe logging for specific components.

```cpp
#include "component_logger.hpp"

// Usage in a component
class MyComponent {
    ComponentLogger<MyComponent> logger;

    void processData() {
        logger.info("Starting data processing");

        try {
            // Process data
            logger.debug("Processed 1000 records");
        } catch (const std::exception& e) {
            logger.error("Processing failed: " + std::string(e.what()));
        }
    }
};
```

**Key Methods:**

- `info(const std::string& message)` - Log informational messages
- `warn(const std::string& message)` - Log warning messages
- `error(const std::string& message)` - Log error messages
- `debug(const std::string& message)` - Log debug messages
- `trace(const std::string& message)` - Log trace messages

**Type Safety Benefits:**

- Automatic component name resolution
- Compile-time type checking
- Consistent logging format across components

#### CoreLogger

The CoreLogger manages logging infrastructure and handler coordination.

```cpp
#include "core_logger.hpp"

// Configure logging
CoreLogger& logger = CoreLogger::instance();
logger.addHandler(std::make_unique<FileLogHandler>("app.log"));
logger.addHandler(std::make_unique<ConsoleLogHandler>());
logger.setLevel(LogLevel::INFO);
```

**Configuration Methods:**

- `addHandler(std::unique_ptr<LogHandler> handler)` - Add log handler
- `removeHandler(const std::string& name)` - Remove log handler
- `setLevel(LogLevel level)` - Set global log level
- `setAsync(bool enabled)` - Enable/disable async logging

### Exception API

#### ETLException Hierarchy

The simplified exception hierarchy provides clear error categorization.

```cpp
#include "etl_exceptions.hpp"

// Validation error
if (!isValidInput(data)) {
    throw ValidationException("Invalid input format", ErrorCode::INVALID_INPUT);
}

// System error
try {
    connectToDatabase();
} catch (const DatabaseException& e) {
    throw SystemException("Database connection failed", ErrorCode::DB_CONNECTION_ERROR, e);
}

// Business logic error
if (balance < amount) {
    throw BusinessException("Insufficient funds", ErrorCode::INSUFFICIENT_FUNDS);
}
```

**Exception Types:**

- **ETLException**: Base exception with error code and correlation ID
- **ValidationException**: Input validation and security errors
- **SystemException**: Infrastructure and system-level errors
- **BusinessException**: Business logic and domain errors

**Exception Features:**

- Automatic correlation ID generation
- JSON serialization for logging
- Error context preservation
- Backward compatibility with legacy codes

### Request Processing API

#### RequestValidator

The RequestValidator provides comprehensive input validation and security checks.

```cpp
#include "request_validator.hpp"

// Create and configure validator
RequestValidator validator;
validator.addRule(std::make_unique<RequiredFieldRule>("username"));
validator.addRule(std::make_unique<EmailFormatRule>("email"));
validator.addRule(std::make_unique<LengthLimitRule>("password", 8, 128));

// Validate request
ValidationResult result = validator.validate(httpRequest);
if (!result.isValid()) {
    // Handle validation errors
    for (const auto& error : result.getErrors()) {
        logger.warn("Validation error: " + error.message);
    }
}
```

**Built-in Validation Rules:**

- **RequiredFieldRule**: Ensures required fields are present
- **EmailFormatRule**: Validates email format
- **LengthLimitRule**: Enforces field length limits
- **NumericRangeRule**: Validates numeric ranges
- **CustomRule**: Extensible custom validation logic

#### ResponseBuilder

The ResponseBuilder provides a fluent interface for HTTP response construction.

```cpp
#include "response_builder.hpp"

// Build successful response
HttpResponse response = ResponseBuilder()
    .status(200)
    .header("Content-Type", "application/json")
    .header("Cache-Control", "no-cache")
    .body(R"({"status": "success", "data": [...]))")
    .build();

// Build error response
HttpResponse errorResponse = ResponseBuilder()
    .status(400)
    .header("Content-Type", "application/json")
    .body(R"({"error": "Invalid request", "code": "VALIDATION_ERROR"})")
    .build();
```

**Builder Methods:**

- `status(int code)` - Set HTTP status code
- `header(const std::string& key, const std::string& value)` - Add HTTP header
- `body(const std::string& content)` - Set response body
- `contentType(const std::string& type)` - Set Content-Type header
- `cors(const std::string& origin)` - Add CORS headers

#### ExceptionMapper

The ExceptionMapper converts exceptions to appropriate HTTP responses.

```cpp
#include "exception_mapper.hpp"

// Configure exception mapping
ExceptionMapper mapper;
mapper.addMapping<ValidationException>(400, "Bad Request");
mapper.addMapping<SystemException>(500, "Internal Server Error");
mapper.addMapping<BusinessException>(422, "Unprocessable Entity");

// Map exception to response
try {
    processRequest(request);
} catch (const ETLException& e) {
    HttpResponse errorResponse = mapper.mapToResponse(e);
    return errorResponse;
}
```

**Mapping Features:**

- Automatic HTTP status code mapping
- Error message formatting
- Correlation ID preservation
- Custom mapping rules support

### WebSocket API

#### WebSocketManager

The WebSocketManager provides high-level WebSocket connection management.

```cpp
#include "websocket_manager_enhanced.hpp"

// Initialize WebSocket manager
WebSocketManager wsManager;
wsManager.initialize(8080);

// Handle connections
wsManager.onConnect([](const std::string& clientId) {
    std::cout << "Client connected: " << clientId << std::endl;
});

wsManager.onDisconnect([](const std::string& clientId) {
    std::cout << "Client disconnected: " << clientId << std::endl;
});

// Broadcast messages
wsManager.broadcast(R"({"type": "update", "data": {...}})");
```

**Connection Management:**

- `initialize(int port)` - Start WebSocket server
- `shutdown()` - Stop WebSocket server
- `getConnectedClients()` - Get list of connected clients
- `isClientConnected(const std::string& clientId)` - Check client connection

**Message Handling:**

- `broadcast(const std::string& message)` - Send to all clients
- `sendTo(const std::string& clientId, const std::string& message)` - Send to specific client
- `onMessage(MessageHandler handler)` - Register message handler
- `onConnect(ConnectHandler handler)` - Register connect handler
- `onDisconnect(DisconnectHandler handler)` - Register disconnect handler

#### ConnectionPool

The ConnectionPool manages WebSocket connection lifecycle and resource optimization.

```cpp
#include "connection_pool_manager.hpp"

// Configure connection pool
ConnectionPool pool;
pool.setMaxConnections(1000);
pool.setConnectionTimeout(std::chrono::seconds(30));
pool.setHealthCheckInterval(std::chrono::seconds(60));

// Use connections
auto connection = pool.acquireConnection();
// Use connection for database operations
pool.releaseConnection(connection);
```

**Pool Configuration:**

- `setMaxConnections(size_t max)` - Set maximum pool size
- `setConnectionTimeout(Duration timeout)` - Set connection timeout
- `setHealthCheckInterval(Duration interval)` - Set health check frequency
- `setRetryAttempts(int attempts)` - Set connection retry attempts

**Pool Operations:**

- `acquireConnection()` - Get connection from pool
- `releaseConnection(Connection* conn)` - Return connection to pool
- `getActiveConnections()` - Get active connection count
- `getAvailableConnections()` - Get available connection count

### Configuration API

#### ConfigManager

The ConfigManager provides centralized configuration management with environment overrides.

```cpp
#include "config_manager.hpp"

// Load configuration
ConfigManager config;
config.loadFromFile("config.json");
config.loadFromEnvironment();

// Access configuration values
std::string dbHost = config.get<std::string>("database.host");
int dbPort = config.get<int>("database.port");
bool debugMode = config.get<bool>("application.debug", false);

// Watch for configuration changes
config.watch("database.host", [](const std::string& newValue) {
    logger.info("Database host changed to: " + newValue);
    // Reconnect to database with new host
});
```

**Configuration Methods:**

- `loadFromFile(const std::string& path)` - Load from JSON file
- `loadFromEnvironment()` - Load from environment variables
- `get<T>(const std::string& key, T defaultValue = T())` - Get configuration value
- `set<T>(const std::string& key, T value)` - Set configuration value
- `watch(const std::string& key, Callback callback)` - Watch for changes

**Type Support:**

- `std::string` - String values
- `int`, `long`, `double` - Numeric values
- `bool` - Boolean values
- `std::vector<T>` - Array values
- `nlohmann::json` - Complex JSON objects

## Advanced API Features

### Template Metaprogramming

#### Type Traits

```cpp
#include "type_traits.hpp"

// Check if type is a component
template<typename T>
void processComponent() {
    if constexpr (is_component_v<T>) {
        ComponentLogger<T> logger;
        logger.info("Processing component");
        // Component-specific logic
    }
}
```

#### Compile-Time Utilities

```cpp
#include "template_utils.hpp"

// Compile-time string hashing
constexpr uint32_t hash = hash_string("component_name");

// Type-safe ID generation
using JobId = StrongType<std::string, struct JobIdTag>;
JobId jobId = JobId::generate();
```

### Concurrency Patterns

#### RAII Lock Helpers

```cpp
#include "lock_utils.hpp"

// Automatic lock management
void processData() {
    OrderedMutex mutex(Order::RESOURCE);
    ScopedTimedLock lock(mutex, std::chrono::milliseconds(100));

    if (lock.owns_lock()) {
        // Critical section
        updateSharedData();
    } else {
        // Handle timeout
        logger.warn("Failed to acquire lock within timeout");
    }
}
```

#### Lock Ordering

```cpp
#include "lock_ordering.hpp"

// Define lock hierarchy
enum class LockOrder {
    CONFIG,
    CONTAINER,
    RESOURCE,
    STATE
};

// Use ordered locks
OrderedMutex configMutex(LockOrder::CONFIG);
OrderedMutex resourceMutex(LockOrder::RESOURCE);
```

### Performance Optimization

#### Connection Pooling

```cpp
#include "connection_pool_manager.hpp"

// Optimized connection usage
ConnectionPool pool("postgresql://localhost/db", 10);

void executeQuery(const std::string& sql) {
    auto connection = pool.acquireConnection();
    // Use connection
    auto result = connection->execute(sql);
    // Connection automatically returned to pool
}
```

#### Async Operations

```cpp
#include "async_utils.hpp"

// Asynchronous processing
Future<Result> processAsync = asyncExecute([this]() {
    // Heavy computation
    return computeResult();
});

// Handle result
processAsync.then([](const Result& result) {
    // Process result
}).catch([](const std::exception& e) {
    // Handle error
});
```

## Error Handling Patterns

### Exception Propagation

```cpp
#include "error_handling.hpp"

// Proper exception handling chain
try {
    validateAndProcess(request);
} catch (const ValidationException& e) {
    logger.warn("Validation failed: " + std::string(e.what()));
    throw; // Re-throw with preserved context
} catch (const SystemException& e) {
    logger.error("System error: " + std::string(e.what()));
    // Attempt recovery or graceful degradation
    return createErrorResponse(e);
} catch (const std::exception& e) {
    logger.error("Unexpected error: " + std::string(e.what()));
    throw SystemException("Unexpected error occurred", ErrorCode::UNEXPECTED_ERROR, e);
}
```

### Error Context Enrichment

```cpp
#include "error_context.hpp"

// Add context to exceptions
void processUserRequest(const HttpRequest& request) {
    ErrorContext context;
    context.add("user_id", request.getHeader("User-Id"));
    context.add("request_id", generateRequestId());
    context.add("endpoint", request.getPath());

    try {
        // Process request
        validateUser(request);
        updateUserData(request);
    } catch (ETLException& e) {
        e.setContext(context);
        throw;
    }
}
```

## Migration Guide

### From Legacy APIs

#### Old Logger Usage

```cpp
// Legacy usage
Logger logger("MyComponent");
logger.log(LogLevel::INFO, "Processing data");
```

#### New ComponentLogger Usage

```cpp
// New usage
ComponentLogger<MyComponent> logger;
logger.info("Processing data");
```

#### Old Exception Usage

```cpp
// Legacy usage
throw std::runtime_error("Database error");
```

#### New Exception Usage

```cpp
// New usage
throw SystemException("Database connection failed", ErrorCode::DB_CONNECTION_ERROR);
```

### Configuration Migration

#### Old Configuration

```cpp
// Legacy configuration access
std::string host = config.getString("db.host");
int port = config.getInt("db.port");
```

#### New Configuration

```cpp
// New configuration access
std::string host = config.get<std::string>("database.host");
int port = config.get<int>("database.port");
```

## Best Practices

### API Usage Guidelines

1. **Use ComponentLogger for all logging**
   - Provides type safety and automatic component identification
   - Consistent logging format across the application

2. **Handle exceptions at appropriate levels**
   - Validation errors at input boundaries
   - System errors with recovery mechanisms
   - Business errors with user-friendly messages

3. **Use RAII for resource management**
   - Automatic cleanup prevents resource leaks
   - Exception-safe resource handling

4. **Leverage fluent interfaces**
   - More readable and maintainable code
   - Reduced boilerplate for common operations

5. **Configure components at startup**
   - Centralized configuration management
   - Environment-specific overrides
   - Runtime configuration updates

### Performance Considerations

1. **Connection pooling for database operations**
   - Reduces connection overhead
   - Improves application performance

2. **Async logging for high-throughput scenarios**
   - Non-blocking logging operations
   - Better application responsiveness

3. **Template-based APIs for compile-time optimization**
   - Zero-cost abstractions
   - Better performance through inlining

4. **Smart pointer usage for memory management**
   - Automatic memory cleanup
   - Prevention of memory leaks

This API documentation provides the foundation for using the refactored ETL Plus system effectively. The APIs are designed to be intuitive, type-safe, and performant while maintaining backward compatibility where possible.
