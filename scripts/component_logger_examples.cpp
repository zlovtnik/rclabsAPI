/**
 * ComponentLogger Usage Examples and Migration Guide
 *
 * This file demonstrates how to use the new ComponentLogger template system
 * and migrate from the old macro-based approach.
 */

#include "component_logger.hpp"
#include <iostream>

/**
 * @brief Demonstrates direct, type-safe usage of ComponentLogger with
 * templates.
 *
 * Shows examples of:
 * - Direct template instantiation for component-level logging
 * (info/debug/warn).
 * - Job-specific logging helpers (infoJob/errorJob) with positional parameters.
 * - Context-aware logging using a string->string map with a transparent hash.
 *
 * Also prints a section header to stdout for the example output.
 */
void example_direct_template_usage() {
  std::cout << "\n=== Direct Template Usage Examples ===" << std::endl;

  // Direct template instantiation - compile-time type safety
  etl::ComponentLogger<etl::ConfigManager>::info(
      "Configuration loaded successfully");
  etl::ComponentLogger<etl::DatabaseManager>::debug(
      "Database connection established");
  etl::ComponentLogger<etl::ETLJobManager>::warn(
      "Job queue is getting full: {} jobs pending", 150);

  // Job-specific logging with template
  etl::ComponentLogger<etl::ETLJobManager>::infoJob(
      "Processing data batch {} of {}", "job_123", 5, 10);
  etl::ComponentLogger<etl::ETLJobManager>::errorJob(
      "Failed to process record: {}", "job_123", "invalid_data");

  // Context-aware logging
  std::unordered_map<std::string, std::string> context = {
      {"user_id", "12345"},
      {"session_id", "abc-def-ghi"},
      {"operation", "data_transform"}};
  etl::ComponentLogger<etl::DataTransformer>::infoWithContext(
      "Data transformation completed", context);
}

/**
 * @brief Demonstrates logging via predefined type aliases.
 *
 * Prints a header and issues example log calls using the project's etl::*
 * logger type aliases to show concise, type-safe usage for regular and
 * job-specific logging at various levels (info, debug, warn, error).
 */
void example_type_aliases() {
  std::cout << "\n=== Type Alias Usage Examples ===" << std::endl;

  // Using predefined type aliases for cleaner code
  etl::ConfigLogger::info("Using type alias for cleaner code");
  etl::DatabaseLogger::debug("Connection pool status: {} active connections",
                             25);
  etl::WebSocketLogger::warn("WebSocket connection limit approaching: {}/{}",
                             95, 100);
  etl::AuthLogger::error("Authentication failed for user: {}",
                         "admin@example.com");

  // Job-specific logging with type aliases
  etl::ETLJobLogger::infoJob("Job started successfully", "job_456");
  etl::ETLJobLogger::debugJob("Processing batch {} with {} records", "job_456",
                              3, 1000);
}

/**
 * @brief Demonstrates logging via backward-compatible convenience macros.
 *
 * Prints a header and exercises the convenience macros that wrap the new
 * template-based ComponentLogger API: general-level macros
 * (info/debug/warn/error/fatal) and job-specific macros that accept a job
 * identifier followed by formatting args.
 *
 * This function is purely demonstrative and has no return value or side effects
 * beyond writing to stdout via the logging macros.
 */
void example_convenience_macros() {
  std::cout << "\n=== Convenience Macro Usage Examples ===" << std::endl;

  // These macros use the new template system under the hood
  CONFIG_LOG_INFO("Server configuration reloaded");
  DB_LOG_DEBUG("Query execution time: {} ms", 45);
  ETL_LOG_WARN("Data validation warning: {} invalid records found", 3);
  WS_LOG_ERROR("WebSocket connection dropped: client {}", "192.168.1.100");
  AUTH_LOG_FATAL("Critical authentication system failure");

  // Job-specific convenience macros
  ETL_LOG_INFO_JOB("Job completed successfully in {} seconds", "job_789", 120);
  ETL_LOG_DEBUG_JOB("Memory usage: {} MB", "job_789", 256);
}

/**
 * @brief Demonstrates usage of template-based logging macros for components.
 *
 * Shows examples of generic component logging macros (COMPONENT_LOG_*)
 * and job-specific variants (COMPONENT_LOG_*_JOB) invoked with explicit
 * component types, format-style messages, and argument lists. Includes
 * samples for debug/info/warn/error/fatal levels and job-scoped logging.
 */
void example_template_macros() {
  std::cout << "\n=== Template-Based Macro Examples ===" << std::endl;

  // Generic template macros for any component type
  COMPONENT_LOG_DEBUG(etl::ConfigManager, "Debug message with parameter: {}",
                      42);
  COMPONENT_LOG_INFO(etl::DatabaseManager,
                     "Info message: connection established");
  COMPONENT_LOG_WARN(etl::ETLJobManager, "Warning: queue size is {}", 500);
  COMPONENT_LOG_ERROR(etl::WebSocketManager, "Error in WebSocket handling");
  COMPONENT_LOG_FATAL(etl::AuthManager, "Fatal authentication error");

  // Job-specific template macros
  COMPONENT_LOG_DEBUG_JOB(etl::ETLJobManager, "Processing batch {}", "job_999",
                          1);
  COMPONENT_LOG_INFO_JOB(etl::DataTransformer, "Transform completed",
                         "job_999");
}

/**
 * @brief Demonstrates performance and metrics logging examples.
 *
 * Prints a header to stdout and emits a set of example performance and metric
 * log events using the template-based loggers (ETLJobLogger, DatabaseLogger,
 * SystemMetricsLogger). Intended to show usage of logPerformance and
 * logMetric APIs for performance timing and numeric metrics.
 */
void example_performance_features() {
  std::cout << "\n=== Performance and Metrics Examples ===" << std::endl;

  // Performance logging
  etl::ETLJobLogger::logPerformance("data_processing", 1250.5);
  etl::DatabaseLogger::logPerformance("query_execution", 45.2);

  // Metrics logging
  etl::SystemMetricsLogger::logMetric("cpu_usage", 75.5, "percent");
  etl::SystemMetricsLogger::logMetric("memory_usage", 2048, "MB");
  etl::ETLJobLogger::logMetric("records_processed", 10000, "count");
}

// Migration example - Before and After
namespace migration_example {

/**
 * @brief Demonstrates the deprecated, hardcoded macro-based logging approach.
 *
 * This stub shows the old pattern where log macros accepted literal component
 * names (strings) instead of type-safe component types. The code is commented
 * out and has no runtime effect; it is kept for historical/migration reference.
 *
 * @deprecated Use the template-based ComponentLogger API (type-safe loggers and
 *             the backward-compatible template macros) instead.
 */
void old_macro_approach() {
  // OLD WAY (deprecated) - hardcoded component strings, no type safety
  // LOG_INFO("ConfigManager", "Configuration loaded");
  // LOG_DEBUG("DatabaseManager", "Connection established");
  // LOG_ERROR("ETLJobManager", "Job failed");
}

/**
 * @brief Demonstrates the new template-based logging API and its
 * backward-compatible macros.
 *
 * Shows usage of compile-time, type-safe ComponentLogger template aliases
 * (ConfigLogger, DatabaseLogger, ETLJobLogger) for common log levels (info,
 * debug, error) and the equivalent backward-compatible macro forms
 * (COMPONENT_LOG_*). This is an example/demo helper that emits the shown log
 * messages at runtime; it has no return value.
 */
void new_template_approach() {
  // NEW WAY - compile-time type safety, performance optimized
  etl::ConfigLogger::info("Configuration loaded");
  etl::DatabaseLogger::debug("Connection established");
  etl::ETLJobLogger::error("Job failed");

  // Or using template macros for backward compatibility
  COMPONENT_LOG_INFO(etl::ConfigManager, "Configuration loaded");
  COMPONENT_LOG_DEBUG(etl::DatabaseManager, "Connection established");
  COMPONENT_LOG_ERROR(etl::ETLJobManager, "Job failed");
}
} // namespace migration_example

int main() {
  std::cout << "=== ComponentLogger Template System Examples ===" << std::endl;

  example_direct_template_usage();
  example_type_aliases();
  example_convenience_macros();
  example_template_macros();
  example_performance_features();

  std::cout << "\n=== All examples completed successfully! ===" << std::endl;
  std::cout << "\nKey Benefits of the Template System:" << std::endl;
  std::cout << "1. Compile-time type safety - component names validated at "
               "compile time"
            << std::endl;
  std::cout << "2. Zero-overhead abstraction - optimized away in release builds"
            << std::endl;
  std::cout << "3. Template parameter validation - prevents typos and wrong "
               "component usage"
            << std::endl;
  std::cout << "4. Consistent API - same interface for all components"
            << std::endl;
  std::cout << "5. Backward compatibility - existing macros still work"
            << std::endl;
  std::cout << "6. Variadic template support - efficient string formatting"
            << std::endl;

  return 0;
}
