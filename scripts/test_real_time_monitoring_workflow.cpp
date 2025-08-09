#include "job_monitor_service.hpp"
#include "notification_service.hpp"
#include "etl_job_manager.hpp"
#include "websocket_manager.hpp"
#include "http_server.hpp"
#include "config_manager.hpp"
#include "logger.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Mock Notification Service to verify alerts
class MockNotificationService : public NotificationService {
public:
    void sendJobFailureAlert(const std::string& jobId, const std::string& error) override {
        failure_alerts++;
        last_job_id = jobId;
        last_error = error;
    }
    int failure_alerts = 0;
    std::string last_job_id;
    std::string last_error;
};

class RealTimeMonitoringWorkflowTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup server components
        config = std::make_shared<ConfigManager>();
        config->load("config/config.json");
        
        logger = std::make_shared<Logger>(*config);
        ws_manager = std::make_shared<WebSocketManager>(*config, *logger);
        notification_service = std::make_shared<MockNotificationService>();
        etl_manager = std::make_shared<ETLJobManager>(*config, *logger);
        monitor_service = std::make_shared<JobMonitorService>(*config, *logger, ws_manager, notification_service);
        etl_manager->setJobMonitorService(monitor_service);

        http_server = std::make_shared<HttpServer>(*config, *logger, ws_manager, monitor_service);

        server_thread = std::thread([this]() {
            http_server->start();
        });
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void TearDown() override {
        http_server->stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }

    std::shared_ptr<ConfigManager> config;
    std::shared_ptr<Logger> logger;
    std::shared_ptr<WebSocketManager> ws_manager;
    std::shared_ptr<MockNotificationService> notification_service;
    std::shared_ptr<ETLJobManager> etl_manager;
    std::shared_ptr<JobMonitorService> monitor_service;
    std::shared_ptr<HttpServer> http_server;
    std::thread server_thread;
};

TEST_F(RealTimeMonitoringWorkflowTest, CompleteJobLifecycle) {
    net::io_context ioc;
    tcp::resolver resolver{ioc};
    websocket::stream<tcp::socket> ws{ioc};

    auto const results = resolver.resolve("127.0.0.1", "8080");
    net::connect(ws.next_layer(), results.begin(), results.end());
    ws.handshake("127.0.0.1:8080", "/");

    // 1. Start a job
    std::string jobId = "job123";
    etl_manager->startJob(jobId, "Test Job");

    // 2. Verify initial WebSocket message
    beast::flat_buffer buffer;
    ws.read(buffer);
    std::string message = beast::buffers_to_string(buffer.data());
    auto json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["type"], "job_status_update");
    EXPECT_EQ(json_msg["payload"]["jobId"], jobId);
    EXPECT_EQ(json_msg["payload"]["status"], "RUNNING");

    // 3. Verify REST API
    // TODO: Add HTTP client to query /api/jobs/{id}/status and verify

    // 4. Simulate job progress
    etl_manager->updateJobProgress(jobId, 50, "Processing data");
    buffer.clear();
    ws.read(buffer);
    message = beast::buffers_to_string(buffer.data());
    json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["payload"]["progress"], 50);

    // 5. Simulate job completion
    etl_manager->finishJob(jobId, "COMPLETED");
    buffer.clear();
    ws.read(buffer);
    message = beast::buffers_to_string(buffer.data());
    json_msg = nlohmann::json::parse(message);
    EXPECT_EQ(json_msg["payload"]["status"], "COMPLETED");

    ws.close(websocket::close_code::normal);
}

TEST_F(RealTimeMonitoringWorkflowTest, MultiClientTest) {
    // TODO: Connect multiple clients and verify they all receive updates.
}

TEST_F(RealTimeMonitoringWorkflowTest, JobFailureNotification) {
    // TODO: Simulate a job failure and verify the notification service is called.
    std::string jobId = "job456";
    etl_manager->startJob(jobId, "Failing Job");
    etl_manager->finishJob(jobId, "FAILED", "Simulated error");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(notification_service->failure_alerts, 1);
    EXPECT_EQ(notification_service->last_job_id, jobId);
    EXPECT_EQ(notification_service->last_error, "Simulated error");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
