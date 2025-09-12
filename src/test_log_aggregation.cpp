#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "log_aggregation_config.hpp"
#include "log_aggregator.hpp"

int main() {
  std::cout << "Testing Log Aggregation System" << std::endl;

  try {
    // Create test destinations
    std::vector<LogDestinationConfig> destinations;

    // File destination
    LogDestinationConfig file_dest;
    file_dest.type = LogDestinationType::FILE;
    file_dest.name = "test_file";
    file_dest.enabled = true;
    file_dest.file_path = "logs/test_aggregated.log";
    file_dest.batch_size = 5;
    file_dest.batch_timeout = std::chrono::seconds(10);
    destinations.push_back(file_dest);

    // HTTP endpoint destination (disabled for test)
    LogDestinationConfig http_dest;
    http_dest.type = LogDestinationType::HTTP_ENDPOINT;
    http_dest.name = "test_http";
    http_dest.enabled = false;
    http_dest.endpoint = "http://localhost:8080/logs";
    http_dest.batch_size = 3;
    http_dest.batch_timeout = std::chrono::seconds(5);
    destinations.push_back(http_dest);

    // Create aggregator
    LogAggregator aggregator(destinations);

    if (!aggregator.initialize()) {
      std::cerr << "Failed to initialize aggregator" << std::endl;
      return 1;
    }

    // Configure structured logger
    auto &structuredLogger = StructuredLogger::getInstance();
    structuredLogger.configureStructuredLogging(true, "test_component");
    structuredLogger.setAggregationEnabled(true);

    std::cout << "Log aggregation initialized successfully" << std::endl;

    // Test structured logging
    std::cout << "Testing structured logging..." << std::endl;

    // Test basic structured logging
    logging::logStructured(LogLevel::INFO, "test", "Test message",
                           {{"user_id", "12345"}, {"action", "login"}},
                           nlohmann::json{{"ip_address", "192.168.1.1"}});

    // Test context logging
    logging::logWithContext(
        LogLevel::WARN, "database", "connection", "Connection timeout occurred",
        {{"host", "localhost"}, {"port", "5432"}, {"timeout", "30s"}});

    // Test component-specific logging
    logging::logApi(
        LogLevel::ERROR, "authentication", "Invalid token provided",
        {{"endpoint", "/api/login"}, {"user_agent", "TestClient/1.0"}});

    logging::logJob(LogLevel::INFO, "job-123", "processing",
                    "Job started successfully",
                    {{"input_records", "1000"}, {"output_format", "json"}});

    logging::logSecurity(LogLevel::WARN, "failed_login",
                         "Multiple failed login attempts",
                         {{"ip_address", "10.0.0.1"}, {"attempts", "5"}});

    // Wait for logs to be processed
    std::cout << "Waiting for logs to be processed..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Get statistics
    const auto &stats = aggregator.getStats();
    std::cout << "Aggregation Statistics:" << std::endl;
    std::cout << "  Total entries processed: "
              << stats.total_entries_processed.load() << std::endl;
    std::cout << "  Entries shipped: " << stats.entries_shipped.load()
              << std::endl;
    std::cout << "  Entries failed: " << stats.entries_failed.load()
              << std::endl;
    std::cout << "  Batches sent: " << stats.batches_sent.load() << std::endl;

    // Shutdown
    structuredLogger.setAggregationEnabled(false);
    aggregator.shutdown();

    std::cout << "Test completed successfully!" << std::endl;
    std::cout
        << "Check logs/test_aggregated.log for the aggregated log entries."
        << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}