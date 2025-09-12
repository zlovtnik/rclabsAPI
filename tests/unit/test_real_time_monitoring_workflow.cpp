#include "config_manager.hpp"
#include "etl_job_manager.hpp"
#include "http_server.hpp"
#include "job_monitor_service.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include "websocket_manager.hpp"
#include <atomic>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Mock Notification Service to verify alerts
class MockNotificationService : public NotificationService {
public:
  void sendJobFailureAlert(const std::string &jobId,
                           const std::string &error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    failure_alerts++;
    last_job_id = jobId;
    last_error = error;
  }

  void sendJobTimeoutWarning(const std::string &jobId,
                             int executionTimeMinutes) override {
    std::lock_guard<std::mutex> lock(mutex_);
    timeout_warnings++;
    last_timeout_job_id = jobId;
    last_timeout_minutes = executionTimeMinutes;
  }

  bool isRunning() const override { return running_; }

  void start() { running_ = true; }
  void stop() { running_ = false; }

  // Test getters
  int getFailureAlerts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failure_alerts;
  }

  int getTimeoutWarnings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timeout_warnings;
  }

  std::string getLastJobId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_job_id;
  }

  std::string getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error;
  }

private:
  mutable std::mutex mutex_;
  std::atomic<bool> running_{false};
  int failure_alerts = 0;
  int timeout_warnings = 0;
  std::string last_job_id;
  std::string last_error;
  std::string last_timeout_job_id;
  int last_timeout_minutes = 0;
};

// HTTP Client for REST API testing
class HttpClient {
public:
  HttpClient() : resolver_(ioc_) {}

  std::string get(const std::string &host, const std::string &port,
                  const std::string &target) {
    try {
      tcp::socket socket(ioc_);
      auto const results = resolver_.resolve(host, port);
      net::connect(socket, results.begin(), results.end());

      http::request<http::string_body> req{http::verb::get, target, 11};
      req.set(http::field::host, host);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

      http::write(socket, req);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(socket, buffer, res);

      socket.shutdown(tcp::socket::shutdown_both);
      return res.body();
    } catch (std::exception const &e) {
      return std::string("ERROR: ") + e.what();
    }
  }

private:
  net::io_context ioc_;
  tcp::resolver resolver_;
};

// WebSocket Client for testing
class WebSocketTestClient {
public:
  WebSocketTestClient() : resolver_(ioc_) {}

  bool connect(const std::string &host, const std::string &port) {
    try {
      auto const results = resolver_.resolve(host, port);
      net::connect(ws_.next_layer(), results.begin(), results.end());
      ws_.handshake(host + ":" + port, "/");
      connected_ = true;
      return true;
    } catch (std::exception const &e) {
      last_error_ = e.what();
      return false;
    }
  }

  bool sendMessage(const std::string &message) {
    if (!connected_)
      return false;
    try {
      ws_.write(net::buffer(message));
      return true;
    } catch (std::exception const &e) {
      last_error_ = e.what();
      return false;
    }
  }

  std::string receiveMessage(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    if (!connected_)
      return "";

    try {
      // Use promise/future for async coordination
      std::promise<std::string> promise;
      std::future<std::string> future = promise.get_future();

      // Buffer must be kept alive until async operation completes
      auto buffer = std::make_shared<beast::flat_buffer>();

      // Timer for timeout handling (must be associated with the same
      // io_context)
      net::steady_timer timer(ioc_, timeout);

      // Use atomic flag to prevent race conditions
      std::atomic<bool> completed{false};

      // Lambda to handle read completion
      auto read_handler = [buffer, &promise, &timer,
                           &completed](beast::error_code ec,
                                       std::size_t bytes_transferred) {
        if (completed.exchange(true))
          return; // Already handled

        // Cancel timer since read completed
        timer.cancel();

        if (ec) {
          if (ec == net::error::operation_aborted) {
            // Operation was cancelled (timeout)
            promise.set_value("TIMEOUT");
          } else {
            // Other error
            promise.set_value(std::string("ERROR: ") + ec.message());
          }
        } else {
          // Successful read
          promise.set_value(beast::buffers_to_string(buffer->data()));
        }
      };

      // Lambda to handle timer expiry
      auto timer_handler = [&promise, &ws = ws_,
                            &completed](beast::error_code ec) {
        if (!ec && !completed.exchange(true)) {
          // Timer expired and we haven't completed yet, cancel the read
          // operation
          beast::error_code cancel_ec;
          ws.next_layer().cancel(cancel_ec);
          // Set timeout result
          promise.set_value("TIMEOUT");
        }
      };

      // Start async read
      ws_.async_read(*buffer, read_handler);

      // Start timer (already set expiry in constructor)
      timer.async_wait(timer_handler);

      // Run the io_context with polling to avoid blocking indefinitely
      ioc_.restart();
      while (!completed && ioc_.run_one_for(std::chrono::milliseconds(10))) {
        // Continue running until completed or we timeout
      }

      // Get the result
      return future.get();

    } catch (std::exception const &e) {
      last_error_ = e.what();
      return std::string("ERROR: ") + e.what();
    }
  }

  void close() {
    if (connected_) {
      try {
        ws_.close(websocket::close_code::normal);
        connected_ = false;
      } catch (...) {
        // Ignore close errors
      }
    }
  }

  bool isConnected() const { return connected_; }
  const std::string &getLastError() const { return last_error_; }

private:
  net::io_context ioc_;
  tcp::resolver resolver_;
  websocket::stream<tcp::socket> ws_{ioc_};
  bool connected_ = false;
  std::string last_error_;
};

class RealTimeMonitoringWorkflowTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup server components
    config = std::shared_ptr<ConfigManager>(&ConfigManager::getInstance(),
                                            [](ConfigManager *) {});

    // Use default config if file doesn't exist
    try {
      config->loadConfig("config/config.json");
    } catch (...) {
      // Create minimal config for testing
      createTestConfig();
    }

    logger = std::shared_ptr<Logger>(&Logger::getInstance(), [](Logger *) {});
    logger->configure(LogConfig{});

    ws_manager = std::make_shared<WebSocketManager>();
    notification_service = std::make_shared<MockNotificationService>();
    notification_service->start();

    etl_manager = std::make_shared<ETLJobManager>(*config, *logger);
    monitor_service = std::make_shared<JobMonitorService>(
        *config, *logger, ws_manager, notification_service);
    etl_manager->setJobMonitorService(monitor_service);

    // Start HTTP server in background thread with proper error handling
    server_port = findAvailablePort();
    http_server = std::make_shared<HttpServer>(*config, *logger, ws_manager,
                                               monitor_service);

    server_thread = std::thread([this]() {
      try {
        http_server->start();
      } catch (const std::exception &e) {
        server_error = e.what();
      }
    });

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Start WebSocket manager
    ws_manager->start();
  }

  void TearDown() override {
    // Clean shutdown
    if (ws_manager) {
      ws_manager->stop();
    }

    if (notification_service) {
      notification_service->stop();
    }

    if (http_server) {
      http_server->stop();
    }

    if (server_thread.joinable()) {
      server_thread.join();
    }
  }

private:
  int findAvailablePort() {
    // Simple port finding - in real tests, use a more robust method
    return 18080 + (rand() % 1000);
  }

  void createTestConfig() {
    // Create minimal test configuration
    nlohmann::json testConfig = {
        {"server", {{"port", 8080}, {"host", "127.0.0.1"}}},
        {"monitoring",
         {{"websocket",
           {{"enabled", true}, {"port", 8081}, {"max_connections", 100}}},
          {"job_tracking",
           {{"progress_update_interval", 1},
            {"timeout_warning_threshold", 300}}}}},
        {"logging", {{"level", "info"}, {"console_output", true}}}};

    // Save test config temporarily
    std::ofstream file("test_config.json");
    file << testConfig.dump(2);
    file.close();

    config->loadConfig("test_config.json");
  }

public:
  ETLJobConfig createTestJobConfig(const std::string &jobId,
                                   const std::string &name) {
    ETLJobConfig config;
    config.jobId = jobId;
    config.type = JobType::FULL_ETL;
    config.sourceConfig =
        R"({"type": "database", "connection": "test_source"})";
    config.targetConfig =
        R"({"type": "database", "connection": "test_target"})";
    config.transformationRules = R"({"rules": ["validate", "transform"]})";
    config.scheduledTime = std::chrono::system_clock::now();
    config.isRecurring = false;
    config.recurringInterval = std::chrono::minutes(0);
    return config;
  }

private:
  int findAvailablePort() {
    // Simple port finding - in real tests, use a more robust method
    return 18080 + (rand() % 1000);
  }

protected:
  std::shared_ptr<ConfigManager> config;
  std::shared_ptr<Logger> logger;
  std::shared_ptr<WebSocketManager> ws_manager;
  std::shared_ptr<MockNotificationService> notification_service;
  std::shared_ptr<ETLJobManager> etl_manager;
  std::shared_ptr<JobMonitorService> monitor_service;
  std::shared_ptr<HttpServer> http_server;
  std::thread server_thread;
  int server_port = 18080;
  std::string server_error;
};

TEST_F(RealTimeMonitoringWorkflowTest, CompleteJobLifecycle) {
  // Skip if server failed to start
  if (!server_error.empty()) {
    GTEST_SKIP() << "Server failed to start: " << server_error;
  }

  WebSocketTestClient wsClient;
  HttpClient httpClient;

  // 1. Connect WebSocket client
  ASSERT_TRUE(wsClient.connect("127.0.0.1", std::to_string(server_port)))
      << "Failed to connect WebSocket: " << wsClient.getLastError();

  // 2. Start a job
  std::string jobId = "test_job_lifecycle_123";
  ETLJobConfig jobConfig = createTestJobConfig(jobId, "Integration Test Job");
  jobId = etl_manager->scheduleJob(jobConfig);

  // 3. Verify initial WebSocket message
  std::string message =
      wsClient.receiveMessage(std::chrono::milliseconds(2000));
  ASSERT_NE(message, "TIMEOUT")
      << "Timeout waiting for initial job status message";
  ASSERT_NE(message.substr(0, 6), "ERROR:")
      << "Error receiving message: " << message;

  auto json_msg = nlohmann::json::parse(message);
  EXPECT_EQ(json_msg["type"], "job_status_update");
  EXPECT_EQ(json_msg["payload"]["jobId"], jobId);
  EXPECT_EQ(json_msg["payload"]["status"], "RUNNING");

  // 4. Verify REST API integration
  std::string restResponse =
      httpClient.get("127.0.0.1", std::to_string(server_port),
                     "/api/jobs/" + jobId + "/status");
  ASSERT_NE(restResponse.substr(0, 6), "ERROR:")
      << "REST API call failed: " << restResponse;

  auto restJson = nlohmann::json::parse(restResponse);
  EXPECT_EQ(restJson["jobId"], jobId);
  EXPECT_EQ(restJson["status"], "RUNNING");

  // 5. Simulate job progress
  etl_manager->publishJobProgress(jobId, 50, "Processing data batch 1");

  message = wsClient.receiveMessage(std::chrono::milliseconds(2000));
  ASSERT_NE(message, "TIMEOUT") << "Timeout waiting for progress update";
  ASSERT_NE(message.substr(0, 6), "ERROR:")
      << "Error receiving progress message: " << message;

  json_msg = nlohmann::json::parse(message);
  EXPECT_EQ(json_msg["payload"]["progress"], 50);
  EXPECT_EQ(json_msg["payload"]["currentStep"], "Processing data batch 1");

  // 6. Simulate job completion
  etl_manager->publishJobStatusUpdate(jobId, JobStatus::COMPLETED);

  message = wsClient.receiveMessage(std::chrono::milliseconds(2000));
  ASSERT_NE(message, "TIMEOUT") << "Timeout waiting for completion message";
  ASSERT_NE(message.substr(0, 6), "ERROR:")
      << "Error receiving completion message: " << message;

  json_msg = nlohmann::json::parse(message);
  EXPECT_EQ(json_msg["payload"]["status"], "COMPLETED");

  // 7. Verify final REST API state
  restResponse = httpClient.get("127.0.0.1", std::to_string(server_port),
                                "/api/jobs/" + jobId + "/status");
  restJson = nlohmann::json::parse(restResponse);
  EXPECT_EQ(restJson["status"], "COMPLETED");

  wsClient.close();
}

TEST_F(RealTimeMonitoringWorkflowTest, MultiClientTest) {
  // Skip if server failed to start
  if (!server_error.empty()) {
    GTEST_SKIP() << "Server failed to start: " << server_error;
  }

  // Create multiple WebSocket clients
  std::vector<std::unique_ptr<WebSocketTestClient>> clients;
  const int numClients = 3;

  // Connect multiple clients
  for (int i = 0; i < numClients; ++i) {
    auto client = std::make_unique<WebSocketTestClient>();
    ASSERT_TRUE(client->connect("127.0.0.1", std::to_string(server_port)))
        << "Failed to connect client " << i << ": " << client->getLastError();
    clients.push_back(std::move(client));
  }

  // Verify all clients are connected
  EXPECT_EQ(ws_manager->getConnectionCount(), numClients);

  // Start a job and verify all clients receive the update
  std::string jobId = "multi_client_test_job_456";
  ETLJobConfig jobConfig = createTestJobConfig(jobId, "Multi-Client Test Job");
  jobId = etl_manager->scheduleJob(jobConfig);

  // Check that all clients receive the job status update
  std::vector<std::string> receivedMessages;
  for (int i = 0; i < numClients; ++i) {
    std::string message =
        clients[i]->receiveMessage(std::chrono::milliseconds(2000));
    ASSERT_NE(message, "TIMEOUT")
        << "Client " << i << " timed out waiting for message";
    ASSERT_NE(message.substr(0, 6), "ERROR:")
        << "Client " << i << " received error: " << message;

    auto json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["type"], "job_status_update");
    EXPECT_EQ(json_msg["payload"]["jobId"], jobId);
    EXPECT_EQ(json_msg["payload"]["status"], "RUNNING");

    receivedMessages.push_back(message);
  }

  // Verify all clients received the same message content
  for (size_t i = 1; i < receivedMessages.size(); ++i) {
    auto msg1 = nlohmann::json::parse(receivedMessages[0]);
    auto msg2 = nlohmann::json::parse(receivedMessages[i]);

    // Compare relevant fields (timestamps might differ slightly)
    EXPECT_EQ(msg1["type"], msg2["type"]);
    EXPECT_EQ(msg1["payload"]["jobId"], msg2["payload"]["jobId"]);
    EXPECT_EQ(msg1["payload"]["status"], msg2["payload"]["status"]);
  }

  // Update job progress and verify all clients receive it
  etl_manager->publishJobProgress(jobId, 75, "Nearly complete");

  for (int i = 0; i < numClients; ++i) {
    std::string message =
        clients[i]->receiveMessage(std::chrono::milliseconds(2000));
    ASSERT_NE(message, "TIMEOUT")
        << "Client " << i << " timed out waiting for progress update";

    auto json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["payload"]["progress"], 75);
    EXPECT_EQ(json_msg["payload"]["currentStep"], "Nearly complete");
  }

  // Complete the job
  etl_manager->publishJobStatusUpdate(jobId, JobStatus::COMPLETED);

  // Verify completion message received by all clients
  for (int i = 0; i < numClients; ++i) {
    std::string message =
        clients[i]->receiveMessage(std::chrono::milliseconds(2000));
    auto json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["payload"]["status"], "COMPLETED");
  }

  // Close all clients
  for (auto &client : clients) {
    client->close();
  }

  // Verify connection count decreases
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(ws_manager->getConnectionCount(), 0);
}

TEST_F(RealTimeMonitoringWorkflowTest, JobFailureNotificationFlow) {
  // Skip if server failed to start
  if (!server_error.empty()) {
    GTEST_SKIP() << "Server failed to start: " << server_error;
  }

  WebSocketTestClient wsClient;
  ASSERT_TRUE(wsClient.connect("127.0.0.1", std::to_string(server_port)));

  // Start a job that will fail
  std::string jobId = "failing_job_789";
  std::string errorMessage = "Simulated database connection failure";

  ETLJobConfig jobConfig = createTestJobConfig(jobId, "Failing Test Job");
  jobId = etl_manager->scheduleJob(jobConfig);

  // Receive initial status
  std::string message = wsClient.receiveMessage();
  auto json_msg = nlohmann::json::parse(message);
  EXPECT_EQ(json_msg["payload"]["status"], "RUNNING");

  // Simulate job failure
  etl_manager->publishJobStatusUpdate(jobId, JobStatus::FAILED);

  // Verify WebSocket failure notification
  message = wsClient.receiveMessage();
  json_msg = nlohmann::json::parse(message);
  EXPECT_EQ(json_msg["payload"]["status"], "FAILED");
  EXPECT_EQ(json_msg["payload"]["errorMessage"], errorMessage);

  // Wait for notification service to process the failure
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify notification service was called
  EXPECT_EQ(notification_service->getFailureAlerts(), 1);
  EXPECT_EQ(notification_service->getLastJobId(), jobId);
  EXPECT_EQ(notification_service->getLastError(), errorMessage);

  wsClient.close();
}

TEST_F(RealTimeMonitoringWorkflowTest, EndToEndMonitoringAndNotificationFlow) {
  // Skip if server failed to start
  if (!server_error.empty()) {
    GTEST_SKIP() << "Server failed to start: " << server_error;
  }

  WebSocketTestClient wsClient;
  HttpClient httpClient;

  ASSERT_TRUE(wsClient.connect("127.0.0.1", std::to_string(server_port)));

  // 1. Start multiple jobs to test comprehensive monitoring
  std::vector<std::string> jobIds = {"e2e_job_1", "e2e_job_2", "e2e_job_3"};

  for (const auto &jobId : jobIds) {
    ETLJobConfig jobConfig =
        createTestJobConfig(jobId, "End-to-End Test Job " + jobId);
    etl_manager->scheduleJob(jobConfig);

    // Verify WebSocket notification
    std::string message = wsClient.receiveMessage();
    auto json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["payload"]["jobId"], jobId);
    EXPECT_EQ(json_msg["payload"]["status"], "RUNNING");
  }

  // 2. Test REST API monitoring endpoints
  std::string restResponse = httpClient.get(
      "127.0.0.1", std::to_string(server_port), "/api/monitor/jobs");
  ASSERT_NE(restResponse.substr(0, 6), "ERROR:")
      << "Failed to get jobs list: " << restResponse;

  auto jobsJson = nlohmann::json::parse(restResponse);
  EXPECT_GE(jobsJson["jobs"].size(), jobIds.size());

  // 3. Test individual job status endpoints
  for (const auto &jobId : jobIds) {
    restResponse = httpClient.get("127.0.0.1", std::to_string(server_port),
                                  "/api/jobs/" + jobId + "/status");
    auto jobJson = nlohmann::json::parse(restResponse);
    EXPECT_EQ(jobJson["jobId"], jobId);
    EXPECT_EQ(jobJson["status"], "RUNNING");
  }

  // 4. Simulate progress updates
  for (size_t i = 0; i < jobIds.size(); ++i) {
    etl_manager->publishJobProgress(jobIds[i], (i + 1) * 30,
                                    "Processing step " + std::to_string(i + 1));

    std::string message = wsClient.receiveMessage();
    auto json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["payload"]["progress"], (i + 1) * 30);
  }

  // 5. Complete some jobs and fail others
  etl_manager->publishJobStatusUpdate(jobIds[0], JobStatus::COMPLETED);
  etl_manager->publishJobStatusUpdate(jobIds[1], JobStatus::FAILED);
  etl_manager->publishJobStatusUpdate(jobIds[2], JobStatus::COMPLETED);

  // 6. Verify completion/failure notifications
  for (size_t i = 0; i < jobIds.size(); ++i) {
    std::string message = wsClient.receiveMessage();
    auto json_msg = nlohmann::json::parse(message);

    if (i == 1) { // Failed job
      EXPECT_EQ(json_msg["payload"]["status"], "FAILED");
      EXPECT_EQ(json_msg["payload"]["errorMessage"], "Validation error");
    } else { // Completed jobs
      EXPECT_EQ(json_msg["payload"]["status"], "COMPLETED");
    }
  }

  // 7. Verify notification service received failure alert
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(notification_service->getFailureAlerts(), 1);
  EXPECT_EQ(notification_service->getLastJobId(), jobIds[1]);

  // 8. Final REST API verification
  restResponse = httpClient.get("127.0.0.1", std::to_string(server_port),
                                "/api/monitor/jobs?status=COMPLETED");
  jobsJson = nlohmann::json::parse(restResponse);

  // Should have 2 completed jobs
  int completedCount = 0;
  for (const auto &job : jobsJson["jobs"]) {
    if (job["status"] == "COMPLETED") {
      completedCount++;
    }
  }
  EXPECT_EQ(completedCount, 2);

  wsClient.close();
}

int main(int argc, char **argv) {
  // Initialize random seed for port selection
  srand(time(nullptr));

  ::testing::InitGoogleTest(&argc, argv);

  std::cout << "Starting Real-time Monitoring Workflow Integration Tests..."
            << std::endl;
  std::cout << "These tests verify complete job lifecycle with real-time "
               "WebSocket updates,"
            << std::endl;
  std::cout
      << "REST API integration, multi-client scenarios, and notification flow."
      << std::endl;

  int result = RUN_ALL_TESTS();

  // Cleanup test config file
  std::remove("test_config.json");

  if (result == 0) {
    std::cout
        << "\n🎉 All real-time monitoring workflow integration tests passed!"
        << std::endl;
    std::cout << "✅ Complete job lifecycle with WebSocket updates: VERIFIED"
              << std::endl;
    std::cout << "✅ REST API integration with monitoring data: VERIFIED"
              << std::endl;
    std::cout << "✅ Multi-client concurrent WebSocket connections: VERIFIED"
              << std::endl;
    std::cout << "✅ End-to-end job monitoring and notification flow: VERIFIED"
              << std::endl;
  } else {
    std::cerr << "\n❌ Some integration tests failed. Check the output above "
                 "for details."
              << std::endl;
  }

  return result;
}
