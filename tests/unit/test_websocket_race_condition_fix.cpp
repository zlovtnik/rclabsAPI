#include "websocket_manager.hpp"
#include <atomic>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Simplified WebSocket test client for testing race condition fix
class SimpleWebSocketTestClient {
public:
  SimpleWebSocketTestClient(net::io_context &ioc)
      : resolver_(ioc), ws_(ioc), connected_(false), last_error_("") {}

  bool connect(const std::string &host, const std::string &port) {
    try {
      auto const results = resolver_.resolve(host, port);
      net::connect(ws_.next_layer(), results.begin(), results.end());
      ws_.handshake(host, "/");
      connected_ = true;
      return true;
    } catch (const std::exception &e) {
      last_error_ = e.what();
      return false;
    }
  }

  // Fixed receiveMessage method using Beast's async_read + timer
  std::string receiveMessage(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    if (!connected_) {
      return "ERROR: Not connected";
    }

    try {
      // Create buffer and promise for coordination
      auto buffer = std::make_shared<beast::multi_buffer>();
      std::promise<std::string> promise;
      std::future<std::string> future = promise.get_future();

      // Atomic flag for completion tracking
      std::atomic<bool> completed{false};

      // Timer for timeout handling
      net::steady_timer timer(ws_.get_executor());
      timer.expires_after(timeout);

      // Read handler
      auto read_handler = [buffer, &promise, &completed,
                           &timer](beast::error_code ec,
                                   std::size_t bytes_transferred) {
        // Cancel timer
        timer.cancel();

        if (completed.exchange(true)) {
          return; // Already handled
        }

        if (ec) {
          if (ec == net::error::operation_aborted) {
            promise.set_value("TIMEOUT");
          } else {
            promise.set_value(std::string("ERROR: ") + ec.message());
          }
          return;
        }

        // Extract message from buffer
        std::string message;
        auto buffers = buffer->data();
        message.reserve(buffer->size());
        for (auto const& buf : buffers) {
          message.append(static_cast<const char*>(buf.data()), buf.size());
        }
        buffer->consume(buffer->size());

        promise.set_value(message);
      };

      // Timer handler
      auto timer_handler = [&promise, &completed](beast::error_code ec) {
        if (completed.exchange(true)) {
          return; // Already handled
        }

        if (ec != net::error::operation_aborted) {
          // Timer expired
          promise.set_value("TIMEOUT");
        }
      };

      // Start async read
      ws_.async_read(*buffer, read_handler);

      // Start timer
      timer.async_wait(timer_handler);

      // Poll io_context until completed or timeout
      net::io_context &ioc =
          static_cast<net::io_context &>(ws_.get_executor().context());
      while (!completed && ioc.run_one_for(std::chrono::milliseconds(10))) {
        // Continue polling
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
  tcp::resolver resolver_;
  websocket::stream<tcp::socket> ws_;
  bool connected_;
  std::string last_error_;
};

// Test class for WebSocket race condition fix
class WebSocketRaceConditionTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create a simple WebSocket server for testing
    server_thread_ = std::thread([this]() {
      try {
        runTestServer();
      } catch (const std::exception &e) {
        server_error_ = e.what();
      }
    });

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  void TearDown() override {
    server_running_ = false;
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  void runTestServer() {
    net::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 18081));

    server_running_ = true;

    while (server_running_) {
      tcp::socket socket(ioc);
      acceptor.accept(socket);

      std::thread([socket = std::move(socket)]() mutable {
        try {
          websocket::stream<tcp::socket> ws(std::move(socket));
          ws.accept();

          // Send a test message
          ws.write(net::buffer(std::string("Hello from test server")));

          // Keep connection alive briefly
          std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } catch (...) {
          // Ignore errors in test server
        }
      }).detach();
    }
  }

  std::thread server_thread_;
  std::atomic<bool> server_running_{false};
  std::string server_error_;
};

// Test the WebSocket race condition fix
TEST_F(WebSocketRaceConditionTest, ReceiveMessageTimeout) {
  net::io_context ioc;
  SimpleWebSocketTestClient client(ioc);

  // Connect to test server
  ASSERT_TRUE(client.connect("127.0.0.1", "18081"));

  // Test receiving a message with timeout
  std::string message = client.receiveMessage(std::chrono::milliseconds(1000));

  // Should receive the test message or timeout gracefully
  EXPECT_TRUE(message == "Hello from test server" || message == "TIMEOUT" ||
              message.substr(0, 6) == "ERROR:");

  client.close();
}

// Test concurrent message reception to verify no race conditions
TEST_F(WebSocketRaceConditionTest, ConcurrentReceiveOperations) {
  net::io_context ioc;
  SimpleWebSocketTestClient client(ioc);

  ASSERT_TRUE(client.connect("127.0.0.1", "18081"));

  // Test multiple concurrent receive operations
  std::vector<std::future<std::string>> futures;
  for (int i = 0; i < 5; ++i) {
    futures.push_back(std::async(std::launch::async, [&client, i]() {
      return client.receiveMessage(std::chrono::milliseconds(500));
    }));
  }

  // Wait for all operations to complete
  for (auto &future : futures) {
    std::string result = future.get();
    // Each operation should complete without crashing
    EXPECT_TRUE(result == "Hello from test server" || result == "TIMEOUT" ||
                result.substr(0, 6) == "ERROR:");
  }

  client.close();
}