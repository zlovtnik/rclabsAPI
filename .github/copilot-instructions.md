# Copilot Instructions for rclabsAPI C++ Project

## Overview
This document provides specific guidelines for GitHub Copilot when assisting with the rclabsAPI C++ project. The project focuses on building a robust API library in C++, emphasizing functional programming principles where applicable, high code quality, and complete implementations without placeholders.

## General Guidelines
- **Language Focus**: All code suggestions must be in C++ (preferably C++17 or later for modern features). Ensure compatibility with the project's build system and dependencies.
- **Code Quality**: Prioritize clean, readable, and maintainable code. Follow best practices such as RAII, smart pointers, and exception safety. Avoid code smells like global variables, magic numbers, or overly complex functions.
- **No Premade Code**: Do not suggest or insert pre-written code snippets from external sources. Instead, guide the user to implement solutions based on project-specific patterns or standard C++ idioms.
- **Completeness**: Never leave TODO comments, placeholders, or incomplete implementations. Ensure all suggested code is fully functional and ready for compilation/testing.
- **Documentation**: Include inline comments for complex logic, but keep them concise. Suggest Doxygen-style comments for public APIs.

## Architecture & System Design

### Core Components Architecture
- **HTTP Server**: Boost.Beast-based HTTP server (`include/http_server.hpp`) - handles REST API requests with multi-threading
- **Request Handler**: Routes API calls to appropriate services (`include/request_handler.hpp`) - implements RESTful endpoints
- **ETL Job Manager**: Core business logic for job scheduling/execution (`include/etl_job_manager.hpp`) - manages ETL workflows
- **Database Manager**: PostgreSQL integration with connection pooling (`include/database_manager.hpp`) - data persistence layer
- **WebSocket Manager**: Real-time communication for job monitoring (`include/websocket_manager.hpp`) - live updates
- **Logger**: Advanced logging with aggregation and multiple destinations (`include/logger.hpp`) - centralized logging

### Data Flow Patterns
- **Request Flow**: HTTP Server → Request Handler → Service Layer → Database/ETL Manager → Response
- **Job Processing**: Job Manager → Data Transformer → Database → WebSocket notifications
- **Configuration**: JSON config files → ConfigManager singleton → Component initialization
- **Logging**: Component logs → Logger → Multiple destinations (file, console, aggregation)

### Key Design Patterns
- **Singleton Pattern**: ConfigManager, Logger use singletons for global access
- **PIMPL Pattern**: DatabaseManager uses opaque pointer for implementation hiding
- **Observer Pattern**: WebSocket notifications for real-time updates
- **Factory Pattern**: Component creation through dependency injection
- **RAII Pattern**: Resource management throughout (smart pointers, scoped locks)

## Build System & Development Workflow

### Build Commands (Critical - Use These)
```bash
# Primary build workflow
make                    # Full build with formatting
make compile           # Just compile (after configure)
make clean             # Clean build artifacts
make rebuild           # Clean + rebuild
make format            # Run clang-format on all sources
make format-check      # Validate code formatting
make run               # Run the application
make help              # Show available commands

# Direct CMake commands
cd build && cmake .. -GNinja  # Configure with Ninja
cd build && ninja             # Build with Ninja
cd build && ninja test_all    # Run all tests
```

### Build System Architecture
- **Primary**: CMake + Ninja (preferred for speed)
- **Fallback**: CMake + Make (detected automatically)
- **OS Detection**: Makefile automatically detects macOS/Linux and selects optimal build system
- **Parallel Builds**: Automatic CPU core detection for optimal compilation speed
- **Dependencies**: Boost, PostgreSQL, nlohmann_json, libcurl, jsoncpp

### Development Conventions
- **Code Formatting**: clang-format enforced on every build (`make format`)
- **Include Guards**: `#pragma once` standard (not `#ifdef` guards)
- **File Organization**: Headers in `include/`, sources in `src/`, tests in `tests/`
- **Naming**: PascalCase for classes/types, snake_case for functions/variables
- **Error Handling**: Exceptions preferred over error codes, with custom exception types
- **Threading**: Boost.Asio for async operations, std::thread for worker threads

## Configuration & Environment

### Configuration Hierarchy
```json
{
  "server": {"address": "0.0.0.0", "port": 8080},
  "database": {"host": "localhost", "port": 5432},
  "logging": {
    "level": "DEBUG",
    "format": "JSON",
    "structured_logging": {
      "enabled": true,
      "aggregation": {
        "enabled": true,
        "destinations": [...]
      }
    }
  }
}
```

### Environment Variables
- `DATABASE_HOST`, `DATABASE_PORT`, `DATABASE_NAME`, `DATABASE_USER`, `DATABASE_PASSWORD`
- `JWT_SECRET` for authentication
- `PORT` to override server port

## Logging & Monitoring

### Structured Logging Patterns
```cpp
// Component-specific logging
LOG_INFO("Database", "Connection established to " + host);
LOG_ERROR("API", "Invalid request: " + error_msg);

// Structured logging with context
logging::logWithContext(LogLevel::WARN, "etl_job", "processing",
                       "Job timeout occurred", {{"job_id", jobId}, {"timeout", "30s"}});

// Component helpers
logging::logApi(LogLevel::ERROR, "authentication", "Token validation failed");
logging::logJob(LogLevel::INFO, jobId, "execution", "Job completed successfully");
logging::logSecurity(LogLevel::WARN, "failed_login", "Multiple auth failures");
```

### Log Aggregation Destinations
- **File**: JSON format with rotation (`logs/aggregated.json`)
- **HTTP Endpoint**: RESTful API integration
- **Elasticsearch**: Bulk API with index patterns
- **Syslog**: Standard system logging
- **CloudWatch**: AWS logging service
- **Splunk**: HEC protocol support

## API Design Patterns

### REST Endpoint Structure
```cpp
// Health & monitoring endpoints
GET  /api/health
GET  /api/monitor/status
GET  /api/monitor/metrics

// Authentication endpoints
POST /api/auth/login
POST /api/auth/logout
GET  /api/auth/profile

// ETL job management
GET  /api/jobs
POST /api/jobs
GET  /api/jobs/{id}
PUT  /api/jobs/{id}/cancel
```

### Request Handler Pattern
```cpp
void RequestHandler::handleRequest(const http::request<http::string_body>& req,
                                  http::response<http::string_body>& res) {
  // Route based on method + path
  if (req.method() == http::verb::get && req.target() == "/api/health") {
    handleHealthCheck(req, res);
  } else if (req.method() == http::verb::post && req.target().starts_with("/api/jobs")) {
    handleCreateJob(req, res);
  }
  // ... route to appropriate handler
}
```

## Database Integration

### Connection Pooling
```cpp
// Database operations through manager
auto dbManager = std::make_shared<DatabaseManager>();
ConnectionConfig config{host, port, db, user, pass};
dbManager->connect(config);

// Schema initialization
dbManager->initializeSchema();

// Query execution with parameters
dbManager->executeQuery("INSERT INTO jobs VALUES (?, ?)", {jobId, status});
auto results = dbManager->selectQuery("SELECT * FROM jobs WHERE status = ?", {"running"});
```

### Repository Pattern
- `UserRepository`, `SessionRepository`, `ETLJobRepository`
- Abstract data access layer
- Consistent CRUD operations across entities

## Testing Patterns

### Unit Test Structure
```cpp
// GTest-based testing
TEST(DatabaseManagerTest, ConnectionTest) {
  DatabaseManager db;
  ConnectionConfig config{/*...*/};
  EXPECT_TRUE(db.connect(config));
  EXPECT_TRUE(db.isConnected());
}

TEST(RequestHandlerTest, HealthCheckTest) {
  auto handler = std::make_shared<RequestHandler>(/*dependencies*/);
  // Test health endpoint
}
```

### Test Organization
- Unit tests in `tests/` directory
- Integration tests for API endpoints
- Performance tests for critical paths
- Test fixtures for common setup

## Functional Programming and Monads
- **Monad Usage Check**: Before suggesting any code, evaluate if monads (e.g., Optional, Either, or custom monadic types) can be integrated or created for the current context. Monads are useful for handling computations with context, such as error handling or chaining operations.
    - If applicable, propose creating or using monads to improve code expressiveness and safety (e.g., using `std::optional` for nullable values or implementing a simple `Result` type for error propagation).
    - Assess feasibility: Consider project constraints like performance, dependencies (e.g., if Boost or range-v3 is available), and team familiarity. If monads are not suitable (e.g., in performance-critical sections), suggest alternative patterns like exceptions or callbacks.
    - Example: For API response handling, suggest a monadic `Result<T, E>` type instead of raw pointers or error codes.
- **Integration**: When using monads, ensure they align with the project's architecture. Provide complete implementations for any new monadic types, including operators like `>>=` (bind) if custom.

## Code Review and Suggestions
- **Quality Checks**: Always review suggested code for:
    - Efficiency: Avoid unnecessary allocations or loops.
    - Safety: Use const-correctness, avoid undefined behavior.
    - Testing: Suggest unit test stubs (e.g., using Google Test) for new functions.
    - Standards: Adhere to C++ Core Guidelines where possible.
- **Refactoring**: If existing code is provided, suggest improvements without altering functionality. Focus on reducing complexity or improving readability.
- **Dependencies**: Only suggest libraries that are already in the project or easily integrable (e.g., STL, Boost). Avoid introducing new dependencies without justification.

## Project-Specific Rules
- **API Design**: Ensure suggestions align with RESTful or similar API patterns, focusing on modularity and extensibility.
- **Error Handling**: Prefer monads or exceptions over error codes for new code.
- **Performance**: Profile-sensitive areas should avoid overhead from monads if it impacts latency.
- **Collaboration**: Suggestions should facilitate pair programming or code reviews by being clear and self-contained.

## Example Workflow
1. User provides a code snippet or description.
2. Copilot checks for monad applicability.
3. Suggests a complete, high-quality C++ implementation.
4. Includes any necessary headers, error handling, and tests.

By following these instructions, Copilot will deliver targeted, high-quality assistance tailored to the rclabsAPI project's needs.