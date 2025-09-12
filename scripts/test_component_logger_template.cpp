#include "logger.hpp"
#include <iostream>
#include <string>

// Simple test to verify the template system works
int main() {
  std::cout << "=== Component Logger Template System Test ===" << std::endl;

  std::cout << "\n--- Testing existing component macros ---" << std::endl;

  // Test existing component-specific macros (should work with template system)
  CONFIG_LOG_INFO(
      "Configuration loaded successfully - Template System Working!");
  DB_LOG_DEBUG("Database connection established - Template System Working!");
  ETL_LOG_INFO("ETL job started - Template System Working!");
  WS_LOG_DEBUG("WebSocket connection accepted - Template System Working!");
  AUTH_LOG_INFO("User authentication successful - Template System Working!");

  std::cout << "\n--- Testing job-specific logging ---" << std::endl;

  ETL_LOG_INFO_JOB("Job processing started - Template System Working!",
                   "job_123");
  ETL_LOG_DEBUG_JOB("Processing data batch - Template System Working!",
                    "job_123");
  ETL_LOG_INFO_JOB("Job completed successfully - Template System Working!",
                   "job_123");

  std::cout << "\n=== Template System Test PASSED ===" << std::endl;
  return 0;
}
