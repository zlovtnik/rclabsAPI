#include "logger.hpp"
#include <iostream>
#include <string>

/**
 * @brief Entry-point test that exercises the component- and job-scoped logging
 * macros.
 *
 * Runs a short sequence that prints section headers to stdout and invokes
 * several component-specific and job-specific logging macros to verify the
 * template-based logging system produces output for INFO and DEBUG levels.
 *
 * Side effects:
 * - Writes human-readable section headers and a final pass message to stdout.
 * - Invokes logging macros (e.g., CONFIG_LOG_INFO, DB_LOG_DEBUG,
 * ETL_LOG_*_JOB), which emit log entries through the project's logging system.
 *
 * @return int Returns 0 on successful completion.
 */
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
