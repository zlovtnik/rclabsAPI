#include "component_logger.hpp"
#include <iostream>
#include <string>

using namespace etl;

// Mock logger functions for testing (since we can't link full logger)
namespace etl {
template <typename Component>
void ComponentLogger<Component>::log_debug(const char *component,
                                           const std::string &message) {
  std::cout << "[DEBUG][" << component << "] " << message << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_info(const char *component,
                                          const std::string &message) {
  std::cout << "[INFO][" << component << "] " << message << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_warn(const char *component,
                                          const std::string &message) {
  std::cout << "[WARN][" << component << "] " << message << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_error(const char *component,
                                           const std::string &message) {
  std::cout << "[ERROR][" << component << "] " << message << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_fatal(const char *component,
                                           const std::string &message) {
  std::cout << "[FATAL][" << component << "] " << message << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_info_job(const char *component,
                                              const std::string &message,
                                              const std::string &jobId) {
  std::cout << "[INFO][" << component << "][Job:" << jobId << "] " << message
            << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_error_job(const char *component,
                                               const std::string &message,
                                               const std::string &jobId) {
  std::cout << "[ERROR][" << component << "][Job:" << jobId << "] " << message
            << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_debug_job(const char *component,
                                               const std::string &message,
                                               const std::string &jobId) {
  std::cout << "[DEBUG][" << component << "][Job:" << jobId << "] " << message
            << std::endl;
}

template <typename Component>
void ComponentLogger<Component>::log_warn_job(const char *component,
                                              const std::string &message,
                                              const std::string &jobId) {
  std::cout << "[WARN][" << component << "][Job:" << jobId << "] " << message
            << std::endl;
}
} // namespace etl

int main() {
  std::cout << "=== ComponentLogger Template System Test ===" << std::endl;

  std::cout << "\n1. Testing compile-time component name resolution..."
            << std::endl;
  std::cout << "ETLJobLogger component: " << ETLJobLogger::get_component_name()
            << std::endl;
  std::cout << "WebSocketLogger component: "
            << WebSocketLogger::get_component_name() << std::endl;
  std::cout << "RequestLogger component: "
            << RequestLogger::get_component_name() << std::endl;
  std::cout << "DatabaseLogger component: "
            << DatabaseLogger::get_component_name() << std::endl;

  std::cout << "\n2. Testing type-safe component logging..." << std::endl;
  ETLJobLogger::info("ETL Job system initialized");
  ETLJobLogger::debug("Debug message from ETL component");

  WebSocketLogger::info("WebSocket connection established");
  WebSocketLogger::warn("Connection pool approaching capacity");

  RequestLogger::info("Processing HTTP request");
  RequestLogger::error("Request validation failed");

  DatabaseLogger::info("Database connection established");

  std::cout << "\n3. Testing job-specific logging..." << std::endl;
  ETLJobLogger::info_job("Job started successfully", "job_12345");
  ETLJobLogger::warn_job("Job taking longer than expected", "job_12345");
  ETLJobLogger::error_job("Job failed with error", "job_12345");

  std::cout << "\n4. Testing formatted logging..." << std::endl;
  ETLJobLogger::info("Processing {} records in {} seconds", 1000, 5.2);
  WebSocketLogger::info("Client {} connected from {}", "user123",
                        "192.168.1.100");
  RequestLogger::info("Request {} completed with status {}", "/api/jobs", 200);

  std::cout << "\n5. Testing all component logger aliases..." << std::endl;
  AuthLogger::info("Authentication service started");
  ConfigLogger::info("Configuration loaded successfully");
  DataTransformLogger::info("Data transformation pipeline ready");
  HttpServerLogger::info("HTTP server listening on port 8080");
  JobMonitorLogger::info("Job monitoring service active");
  NotificationLogger::info("Notification service connected");
  WebSocketFilterLogger::info("WebSocket filter manager initialized");

  std::cout << "\n=== All tests completed successfully ===" << std::endl;

  return 0;
}
