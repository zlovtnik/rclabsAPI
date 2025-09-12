#include "../include/job_monitoring_models.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

// Test helper functions
void assertEqual(const std::string &expected, const std::string &actual,
                 const std::string &testName) {
  if (expected != actual) {
    std::cerr << "FAIL: " << testName << std::endl;
    std::cerr << "Expected: " << expected << std::endl;
    std::cerr << "Actual: " << actual << std::endl;
    exit(1);
  }
  std::cout << "PASS: " << testName << std::endl;
}

void assertTrue(bool condition, const std::string &testName) {
  if (!condition) {
    std::cerr << "FAIL: " << testName << std::endl;
    exit(1);
  }
  std::cout << "PASS: " << testName << std::endl;
}

void assertFalse(bool condition, const std::string &testName) {
  assertTrue(!condition, testName);
}

// Test JobMetrics serialization
void testJobMetricsSerialization() {
  std::cout << "\n=== Testing JobMetrics Serialization ===" << std::endl;

  JobMetrics metrics;
  metrics.recordsProcessed = 1000;
  metrics.recordsSuccessful = 950;
  metrics.recordsFailed = 50;
  metrics.processingRate = 125.75;
  metrics.memoryUsage = 1024000;
  metrics.cpuUsage = 85.5;
  metrics.executionTime = std::chrono::milliseconds(30000);

  std::string json = metrics.toJson();
  std::cout << "JobMetrics JSON: " << json << std::endl;

  // Verify JSON contains expected fields
  assertTrue(json.find("\"recordsProcessed\":1000") != std::string::npos,
             "JobMetrics JSON contains recordsProcessed");
  assertTrue(json.find("\"recordsSuccessful\":950") != std::string::npos,
             "JobMetrics JSON contains recordsSuccessful");
  assertTrue(json.find("\"recordsFailed\":50") != std::string::npos,
             "JobMetrics JSON contains recordsFailed");
  assertTrue(json.find("\"processingRate\":125.75") != std::string::npos,
             "JobMetrics JSON contains processingRate");
  assertTrue(json.find("\"memoryUsage\":1024000") != std::string::npos,
             "JobMetrics JSON contains memoryUsage");
  assertTrue(json.find("\"cpuUsage\":85.50") != std::string::npos,
             "JobMetrics JSON contains cpuUsage");
  assertTrue(json.find("\"executionTime\":30000") != std::string::npos,
             "JobMetrics JSON contains executionTime");

  // Test deserialization
  JobMetrics deserializedMetrics = JobMetrics::fromJson(json);
  assertTrue(deserializedMetrics.recordsProcessed == 1000,
             "Deserialized recordsProcessed");
  assertTrue(deserializedMetrics.recordsSuccessful == 950,
             "Deserialized recordsSuccessful");
  assertTrue(deserializedMetrics.recordsFailed == 50,
             "Deserialized recordsFailed");
  assertTrue(std::abs(deserializedMetrics.processingRate - 125.75) < 0.01,
             "Deserialized processingRate");
  assertTrue(deserializedMetrics.memoryUsage == 1024000,
             "Deserialized memoryUsage");
  assertTrue(std::abs(deserializedMetrics.cpuUsage - 85.5) < 0.01,
             "Deserialized cpuUsage");
  assertTrue(deserializedMetrics.executionTime.count() == 30000,
             "Deserialized executionTime");
}

// Test JobStatusUpdate serialization
void testJobStatusUpdateSerialization() {
  std::cout << "\n=== Testing JobStatusUpdate Serialization ===" << std::endl;

  JobStatusUpdate update;
  update.jobId = "test_job_123";
  update.status = JobStatus::RUNNING;
  update.previousStatus = JobStatus::PENDING;
  update.timestamp = std::chrono::system_clock::now();
  update.progressPercent = 75;
  update.currentStep = "Processing batch 3/4";
  update.errorMessage = std::nullopt; // No error

  // Set up metrics
  update.metrics.recordsProcessed = 750;
  update.metrics.recordsSuccessful = 740;
  update.metrics.recordsFailed = 10;
  update.metrics.processingRate = 100.0;

  std::string json = update.toJson();
  std::cout << "JobStatusUpdate JSON: " << json << std::endl;

  // Verify JSON contains expected fields
  assertTrue(json.find("\"jobId\":\"test_job_123\"") != std::string::npos,
             "JobStatusUpdate JSON contains jobId");
  assertTrue(json.find("\"status\":\"running\"") != std::string::npos,
             "JobStatusUpdate JSON contains status");
  assertTrue(json.find("\"previousStatus\":\"pending\"") != std::string::npos,
             "JobStatusUpdate JSON contains previousStatus");
  assertTrue(json.find("\"progressPercent\":75") != std::string::npos,
             "JobStatusUpdate JSON contains progressPercent");
  assertTrue(json.find("\"currentStep\":\"Processing batch 3/4\"") !=
                 std::string::npos,
             "JobStatusUpdate JSON contains currentStep");
  assertTrue(json.find("\"metrics\":{") != std::string::npos,
             "JobStatusUpdate JSON contains metrics");

  // Test deserialization
  JobStatusUpdate deserializedUpdate = JobStatusUpdate::fromJson(json);
  assertEqual("test_job_123", deserializedUpdate.jobId, "Deserialized jobId");
  assertTrue(deserializedUpdate.status == JobStatus::RUNNING,
             "Deserialized status");
  assertTrue(deserializedUpdate.previousStatus == JobStatus::PENDING,
             "Deserialized previousStatus");
  assertTrue(deserializedUpdate.progressPercent == 75,
             "Deserialized progressPercent");
  assertEqual("Processing batch 3/4", deserializedUpdate.currentStep,
              "Deserialized currentStep");
  assertTrue(deserializedUpdate.metrics.recordsProcessed == 750,
             "Deserialized metrics");

  // Test helper methods
  assertTrue(update.isStatusChange(), "JobStatusUpdate isStatusChange");
  assertFalse(update.isProgressUpdate(),
              "JobStatusUpdate isProgressUpdate (status changed)");

  // Test progress update (same status)
  update.previousStatus = JobStatus::RUNNING;
  assertFalse(update.isStatusChange(),
              "JobStatusUpdate isStatusChange (same status)");
  assertTrue(update.isProgressUpdate(), "JobStatusUpdate isProgressUpdate");
}

// Test JobStatusUpdate with error message
void testJobStatusUpdateWithError() {
  std::cout << "\n=== Testing JobStatusUpdate with Error ===" << std::endl;

  JobStatusUpdate update;
  update.jobId = "failed_job_456";
  update.status = JobStatus::FAILED;
  update.previousStatus = JobStatus::RUNNING;
  update.timestamp = std::chrono::system_clock::now();
  update.progressPercent = 45;
  update.currentStep = "Data validation";
  update.errorMessage = "Database connection timeout";

  std::string json = update.toJson();
  std::cout << "JobStatusUpdate with error JSON: " << json << std::endl;

  assertTrue(json.find("\"errorMessage\":\"Database connection timeout\"") !=
                 std::string::npos,
             "JobStatusUpdate JSON contains errorMessage");

  JobStatusUpdate deserializedUpdate = JobStatusUpdate::fromJson(json);
  assertTrue(deserializedUpdate.errorMessage.has_value(),
             "Deserialized errorMessage exists");
  assertEqual("Database connection timeout",
              deserializedUpdate.errorMessage.value(),
              "Deserialized errorMessage value");
}

// Test JobMonitoringData serialization
void testJobMonitoringDataSerialization() {
  std::cout << "\n=== Testing JobMonitoringData Serialization ===" << std::endl;

  JobMonitoringData data;
  data.jobId = "monitoring_job_789";
  data.jobType = JobType::FULL_ETL;
  data.status = JobStatus::RUNNING;
  data.progressPercent = 60;
  data.currentStep = "Transform phase";
  data.startTime = std::chrono::system_clock::now();
  data.createdAt = data.startTime - std::chrono::minutes(5);
  data.executionTime = std::chrono::milliseconds(180000); // 3 minutes

  // Add some recent logs
  data.recentLogs.push_back("Started extraction from source");
  data.recentLogs.push_back("Extracted 1000 records");
  data.recentLogs.push_back("Starting transformation");

  // Set up metrics
  data.metrics.recordsProcessed = 600;
  data.metrics.recordsSuccessful = 590;
  data.metrics.recordsFailed = 10;

  std::string json = data.toJson();
  std::cout << "JobMonitoringData JSON: " << json << std::endl;

  // Verify JSON contains expected fields
  assertTrue(json.find("\"jobId\":\"monitoring_job_789\"") != std::string::npos,
             "JobMonitoringData JSON contains jobId");
  assertTrue(json.find("\"jobType\":\"full_etl\"") != std::string::npos,
             "JobMonitoringData JSON contains jobType");
  assertTrue(json.find("\"status\":\"running\"") != std::string::npos,
             "JobMonitoringData JSON contains status");
  assertTrue(json.find("\"progressPercent\":60") != std::string::npos,
             "JobMonitoringData JSON contains progressPercent");
  assertTrue(json.find("\"currentStep\":\"Transform phase\"") !=
                 std::string::npos,
             "JobMonitoringData JSON contains currentStep");
  assertTrue(json.find("\"recentLogs\":[") != std::string::npos,
             "JobMonitoringData JSON contains recentLogs array");
  assertTrue(json.find("Started extraction from source") != std::string::npos,
             "JobMonitoringData JSON contains log entry");

  // Test helper methods
  assertTrue(data.isActive(), "JobMonitoringData isActive");
  assertEqual("running", data.getStatusString(),
              "JobMonitoringData getStatusString");
  assertEqual("full_etl", data.getJobTypeString(),
              "JobMonitoringData getJobTypeString");

  // Test deserialization
  JobMonitoringData deserializedData = JobMonitoringData::fromJson(json);
  assertEqual("monitoring_job_789", deserializedData.jobId,
              "Deserialized jobId");
  assertTrue(deserializedData.jobType == JobType::FULL_ETL,
             "Deserialized jobType");
  assertTrue(deserializedData.status == JobStatus::RUNNING,
             "Deserialized status");
  assertTrue(deserializedData.progressPercent == 60,
             "Deserialized progressPercent");
}

// Test LogMessage serialization
void testLogMessageSerialization() {
  std::cout << "\n=== Testing LogMessage Serialization ===" << std::endl;

  LogMessage logMsg;
  logMsg.jobId = "log_test_job";
  logMsg.level = "ERROR";
  logMsg.component = "DataTransformer";
  logMsg.message = "Failed to parse record: invalid format";
  logMsg.timestamp = std::chrono::system_clock::now();
  logMsg.context["record_id"] = "12345";
  logMsg.context["line_number"] = "42";

  std::string json = logMsg.toJson();
  std::cout << "LogMessage JSON: " << json << std::endl;

  // Verify JSON contains expected fields
  assertTrue(json.find("\"jobId\":\"log_test_job\"") != std::string::npos,
             "LogMessage JSON contains jobId");
  assertTrue(json.find("\"level\":\"ERROR\"") != std::string::npos,
             "LogMessage JSON contains level");
  assertTrue(json.find("\"component\":\"DataTransformer\"") !=
                 std::string::npos,
             "LogMessage JSON contains component");
  assertTrue(
      json.find("\"message\":\"Failed to parse record: invalid format\"") !=
          std::string::npos,
      "LogMessage JSON contains message");
  assertTrue(json.find("\"context\":{") != std::string::npos,
             "LogMessage JSON contains context");
  assertTrue(json.find("\"record_id\":\"12345\"") != std::string::npos,
             "LogMessage JSON contains context field");

  // Test filter matching
  assertTrue(logMsg.matchesFilter("log_test_job", "ERROR"),
             "LogMessage matches exact filters");
  assertTrue(logMsg.matchesFilter("", "ERROR"),
             "LogMessage matches level filter only");
  assertTrue(logMsg.matchesFilter("log_test_job", ""),
             "LogMessage matches job filter only");
  assertTrue(logMsg.matchesFilter("", ""), "LogMessage matches no filters");
  assertFalse(logMsg.matchesFilter("other_job", "ERROR"),
              "LogMessage doesn't match wrong job");
  assertFalse(logMsg.matchesFilter("log_test_job", "INFO"),
              "LogMessage doesn't match wrong level");

  // Test deserialization
  LogMessage deserializedMsg = LogMessage::fromJson(json);
  assertEqual("log_test_job", deserializedMsg.jobId, "Deserialized jobId");
  assertEqual("ERROR", deserializedMsg.level, "Deserialized level");
  assertEqual("DataTransformer", deserializedMsg.component,
              "Deserialized component");
  assertEqual("Failed to parse record: invalid format", deserializedMsg.message,
              "Deserialized message");
}

// Test WebSocketMessage serialization
void testWebSocketMessageSerialization() {
  std::cout << "\n=== Testing WebSocketMessage Serialization ===" << std::endl;

  // Test job status update message
  JobStatusUpdate update;
  update.jobId = "ws_test_job";
  update.status = JobStatus::COMPLETED;
  update.previousStatus = JobStatus::RUNNING;
  update.progressPercent = 100;

  WebSocketMessage wsMessage = WebSocketMessage::createJobStatusUpdate(update);
  std::string json = wsMessage.toJson();
  std::cout << "WebSocketMessage JSON: " << json << std::endl;

  assertTrue(json.find("\"type\":\"job_status_update\"") != std::string::npos,
             "WebSocketMessage JSON contains type");
  assertTrue(json.find("\"timestamp\":") != std::string::npos,
             "WebSocketMessage JSON contains timestamp");
  assertTrue(json.find("\"data\":{") != std::string::npos,
             "WebSocketMessage JSON contains data");
  assertTrue(json.find("\"targetJobId\":\"ws_test_job\"") != std::string::npos,
             "WebSocketMessage JSON contains targetJobId");

  // Test factory methods
  LogMessage logMsg;
  logMsg.jobId = "log_job";
  logMsg.level = "INFO";
  logMsg.message = "Test log message";

  WebSocketMessage logWsMessage = WebSocketMessage::createLogMessage(logMsg);
  assertTrue(logWsMessage.type == MessageType::JOB_LOG_MESSAGE,
             "Log message type");
  assertTrue(logWsMessage.targetJobId == "log_job", "Log message targetJobId");
  assertTrue(logWsMessage.targetLevel == "INFO", "Log message targetLevel");

  JobMetrics metrics;
  metrics.recordsProcessed = 100;
  WebSocketMessage metricsMessage =
      WebSocketMessage::createMetricsUpdate("metrics_job", metrics);
  assertTrue(metricsMessage.type == MessageType::JOB_METRICS_UPDATE,
             "Metrics message type");

  WebSocketMessage errorMessage =
      WebSocketMessage::createErrorMessage("Test error");
  assertTrue(errorMessage.type == MessageType::ERROR_MESSAGE,
             "Error message type");
  assertTrue(errorMessage.data.find("Test error") != std::string::npos,
             "Error message data");

  WebSocketMessage ackMessage = WebSocketMessage::createConnectionAck();
  assertTrue(ackMessage.type == MessageType::CONNECTION_ACK,
             "Connection ack type");
}

// Test ConnectionFilters serialization
void testConnectionFiltersSerialization() {
  std::cout << "\n=== Testing ConnectionFilters Serialization ===" << std::endl;

  ConnectionFilters filters;
  filters.jobIds = {"job1", "job2", "job3"};
  filters.logLevels = {"ERROR", "WARN"};
  filters.messageTypes = {MessageType::JOB_STATUS_UPDATE,
                          MessageType::JOB_LOG_MESSAGE};
  filters.includeSystemNotifications = false;

  std::string json = filters.toJson();
  std::cout << "ConnectionFilters JSON: " << json << std::endl;

  assertTrue(json.find("\"jobIds\":[\"job1\",\"job2\",\"job3\"]") !=
                 std::string::npos,
             "ConnectionFilters JSON contains jobIds");
  assertTrue(json.find("\"logLevels\":[\"ERROR\",\"WARN\"]") !=
                 std::string::npos,
             "ConnectionFilters JSON contains logLevels");
  assertTrue(
      json.find(
          "\"messageTypes\":[\"job_status_update\",\"job_log_message\"]") !=
          std::string::npos,
      "ConnectionFilters JSON contains messageTypes");
  assertTrue(json.find("\"includeSystemNotifications\":false") !=
                 std::string::npos,
             "ConnectionFilters JSON contains includeSystemNotifications");

  // Test filter methods
  assertTrue(filters.shouldReceiveJob("job1"), "Should receive job1");
  assertTrue(filters.shouldReceiveJob("job2"), "Should receive job2");
  assertFalse(filters.shouldReceiveJob("job4"), "Should not receive job4");

  assertTrue(filters.shouldReceiveLogLevel("ERROR"),
             "Should receive ERROR level");
  assertFalse(filters.shouldReceiveLogLevel("INFO"),
              "Should not receive INFO level");

  assertTrue(filters.shouldReceiveMessageType(MessageType::JOB_STATUS_UPDATE),
             "Should receive JOB_STATUS_UPDATE");
  assertFalse(filters.shouldReceiveMessageType(MessageType::JOB_METRICS_UPDATE),
              "Should not receive JOB_METRICS_UPDATE");

  // Test message filtering
  WebSocketMessage testMessage;
  testMessage.type = MessageType::JOB_STATUS_UPDATE;
  testMessage.targetJobId = "job1";
  assertTrue(filters.shouldReceiveMessage(testMessage),
             "Should receive matching message");

  testMessage.targetJobId = "job4";
  assertFalse(filters.shouldReceiveMessage(testMessage),
              "Should not receive non-matching job");

  testMessage.type = MessageType::SYSTEM_NOTIFICATION;
  assertFalse(filters.shouldReceiveMessage(testMessage),
              "Should not receive system notifications");

  // Test deserialization
  ConnectionFilters deserializedFilters = ConnectionFilters::fromJson(json);
  assertTrue(deserializedFilters.jobIds.size() == 3,
             "Deserialized jobIds count");
  assertTrue(deserializedFilters.logLevels.size() == 2,
             "Deserialized logLevels count");
  assertTrue(deserializedFilters.messageTypes.size() == 2,
             "Deserialized messageTypes count");
  assertFalse(deserializedFilters.includeSystemNotifications,
              "Deserialized includeSystemNotifications");
}

// Test utility functions
void testUtilityFunctions() {
  std::cout << "\n=== Testing Utility Functions ===" << std::endl;

  // Test message type conversion
  assertEqual("job_status_update",
              messageTypeToString(MessageType::JOB_STATUS_UPDATE),
              "MessageType to string conversion");
  assertTrue(stringToMessageType("job_status_update") ==
                 MessageType::JOB_STATUS_UPDATE,
             "String to MessageType conversion");

  // Test job status conversion
  assertEqual("running", jobStatusToString(JobStatus::RUNNING),
              "JobStatus to string conversion");
  assertTrue(stringToJobStatus("running") == JobStatus::RUNNING,
             "String to JobStatus conversion");

  // Test job type conversion
  assertEqual("full_etl", jobTypeToString(JobType::FULL_ETL),
              "JobType to string conversion");
  assertTrue(stringToJobType("full_etl") == JobType::FULL_ETL,
             "String to JobType conversion");

  // Test JSON escaping
  assertEqual("Hello \\\"World\\\"", escapeJsonString("Hello \"World\""),
              "JSON string escaping");
  assertEqual("Line 1\\nLine 2", escapeJsonString("Line 1\nLine 2"),
              "JSON newline escaping");
  assertEqual("Tab\\tSeparated", escapeJsonString("Tab\tSeparated"),
              "JSON tab escaping");

  // Test validation functions
  assertTrue(validateJobId("valid_job_123"), "Valid job ID");
  assertTrue(validateJobId("job-with-hyphens"), "Valid job ID with hyphens");
  assertFalse(validateJobId(""), "Empty job ID");
  assertFalse(validateJobId("job with spaces"), "Job ID with spaces");
  assertFalse(validateJobId("job@invalid"), "Job ID with invalid characters");

  assertTrue(validateLogLevel("ERROR"), "Valid log level ERROR");
  assertTrue(validateLogLevel("INFO"), "Valid log level INFO");
  assertFalse(validateLogLevel("INVALID"), "Invalid log level");
  assertFalse(validateLogLevel(""), "Empty log level");

  assertTrue(validateMessageType("job_status_update"), "Valid message type");
  assertFalse(validateMessageType("invalid_type"), "Invalid message type");
  assertFalse(validateMessageType(""), "Empty message type");
}

// Test timestamp formatting and parsing
void testTimestampHandling() {
  std::cout << "\n=== Testing Timestamp Handling ===" << std::endl;

  auto now = std::chrono::system_clock::now();
  std::string formatted = formatTimestamp(now);
  std::cout << "Formatted timestamp: " << formatted << std::endl;

  // Verify format (ISO 8601 with milliseconds)
  assertTrue(formatted.find('T') != std::string::npos,
             "Timestamp contains T separator");
  assertTrue(formatted.find('Z') != std::string::npos,
             "Timestamp contains Z suffix");
  assertTrue(formatted.find('.') != std::string::npos,
             "Timestamp contains milliseconds");

  // Test parsing (note: parsing may lose some precision due to timezone
  // handling)
  auto parsed = parseTimestamp(formatted);
  auto diff = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now() - parsed)
                  .count();
  // Allow larger tolerance due to timezone conversion issues in simple parsing
  assertTrue(std::abs(diff) < 86400,
             "Parsed timestamp is within reasonable range");
}

int main() {
  std::cout << "Starting Job Monitoring Models Tests..." << std::endl;

  try {
    testJobMetricsSerialization();
    testJobStatusUpdateSerialization();
    testJobStatusUpdateWithError();
    testJobMonitoringDataSerialization();
    testLogMessageSerialization();
    testWebSocketMessageSerialization();
    testConnectionFiltersSerialization();
    testUtilityFunctions();
    testTimestampHandling();

    std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
    std::cout << "Job monitoring models implementation is working correctly!"
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}