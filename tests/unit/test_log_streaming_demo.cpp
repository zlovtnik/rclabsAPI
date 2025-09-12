#include "../include/job_monitoring_models.hpp"
#include "../include/logger.hpp"
#include "../include/transparent_string_hash.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

void demonstrateLogStreaming() {
  std::cout << "\n=== Log Streaming Functionality Demonstration ==="
            << std::endl;

  Logger &logger = Logger::getInstance();

  // Configure logger for demonstration (disable streaming to avoid WebSocket
  // dependency)
  LogConfig config;
  config.enableRealTimeStreaming = false; // Keep disabled for demo
  config.streamingQueueSize = 100;
  config.streamAllLevels = true;
  config.consoleOutput = true;
  config.fileOutput = false;

  logger.configure(config);

  std::cout << "\n1. Testing job-specific logging methods:" << std::endl;

  // Test job-specific logging methods
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
      context = {{"step", "data_validation"}, {"records", "1000"}};

  logger.debugForJob("ETLJobManager", "Starting job validation", "job_001",
                     context);
  logger.infoForJob("ETLJobManager", "Processing batch 1 of 5", "job_001",
                    context);
  logger.warnForJob("ETLJobManager", "Found 3 invalid records", "job_001",
                    context);
  logger.errorForJob("ETLJobManager", "Failed to process record ID 12345",
                     "job_001", context);

  std::cout << "\n2. Testing job-specific macros:" << std::endl;

  // Test the new job-specific macros
  ETL_LOG_DEBUG_JOB("Debug message using macro", "job_002", context);
  ETL_LOG_INFO_JOB("Info message using macro", "job_002", context);
  ETL_LOG_WARN_JOB("Warning message using macro", "job_002", context);
  ETL_LOG_ERROR_JOB("Error message using macro", "job_002", context);

  std::cout << "\n3. Testing log filtering configuration:" << std::endl;

  // Test filter management
  logger.addStreamingJobFilter("important_job");
  logger.addStreamingJobFilter("critical_job");

  std::cout << "Added job filters for: important_job, critical_job"
            << std::endl;

  logger.removeStreamingJobFilter("important_job");
  std::cout << "Removed filter for: important_job" << std::endl;

  logger.clearStreamingJobFilter();
  std::cout << "Cleared all job filters" << std::endl;

  std::cout << "\n4. Testing LogMessage creation and serialization:"
            << std::endl;

  // Create a LogMessage directly
  LogMessage logMsg;
  logMsg.jobId = "demo_job";
  logMsg.level = "INFO";
  logMsg.component = "DemoComponent";
  logMsg.message = "This is a demo log message";
  logMsg.timestamp = std::chrono::system_clock::now();
  logMsg.context = {{"user_id", "demo_user"}, {"operation", "demo_operation"}};

  std::string json = logMsg.toJson();
  std::cout << "Serialized LogMessage JSON:" << std::endl;
  std::cout << json << std::endl;

  // Test filtering
  std::cout << "\n5. Testing log message filtering:" << std::endl;

  bool matchesJob = logMsg.matchesFilter("demo_job", "");
  bool matchesLevel = logMsg.matchesFilter("", "INFO");
  bool matchesBoth = logMsg.matchesFilter("demo_job", "INFO");
  bool matchesNeither = logMsg.matchesFilter("other_job", "ERROR");

  std::cout << "Matches job filter 'demo_job': " << (matchesJob ? "Yes" : "No")
            << std::endl;
  std::cout << "Matches level filter 'INFO': " << (matchesLevel ? "Yes" : "No")
            << std::endl;
  std::cout << "Matches both filters: " << (matchesBoth ? "Yes" : "No")
            << std::endl;
  std::cout << "Matches wrong filters: " << (matchesNeither ? "Yes" : "No")
            << std::endl;

  std::cout << "\n6. Testing logger metrics:" << std::endl;

  LogMetrics metrics = logger.getMetrics();
  std::cout << "Total messages logged: " << metrics.totalMessages.load()
            << std::endl;
  std::cout << "Error messages: " << metrics.errorCount.load() << std::endl;
  std::cout << "Warning messages: " << metrics.warningCount.load() << std::endl;
  std::cout << "Dropped messages: " << metrics.droppedMessages.load()
            << std::endl;

  logger.flush();

  std::cout << "\n✓ Log streaming functionality demonstration completed!"
            << std::endl;
}

int main() {
  std::cout << "Logger Real-time Streaming Functionality Demo" << std::endl;
  std::cout << "=============================================" << std::endl;

  try {
    demonstrateLogStreaming();

    std::cout << "\n🎉 Demo completed successfully!" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "❌ Demo failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "❌ Demo failed with unknown exception" << std::endl;
    return 1;
  }

  // Clean shutdown
  Logger::getInstance().shutdown();

  return 0;
}