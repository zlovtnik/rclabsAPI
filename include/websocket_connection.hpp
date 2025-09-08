#pragma once

#include "job_monitoring_models.hpp"
#include "websocket_connection_recovery.hpp"
#include <atomic>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Forward declaration
class WebSocketManager;

class WebSocketConnection
    : public std::enable_shared_from_this<WebSocketConnection> {
public:
  using MessageHandler = std::function<void(const std::string &)>;
  using CloseHandler = std::function<void(const std::string &)>;
  using ErrorHandler =
      std::function<void(const std::string &, const std::string &)>;

  WebSocketConnection(tcp::socket socket,
                      std::weak_ptr<WebSocketManager> manager);
  ~WebSocketConnection();

  void start();
  void send(const std::string &message);
  void close();

  const std::string &getId() const { return connectionId_; }
  bool isOpen() const { return isOpen_.load(); }
  bool isHealthy() const;

  // Error handling and recovery
  void
  setRecoveryConfig(const websocket_recovery::ConnectionRecoveryConfig &config);
  const websocket_recovery::ConnectionRecoveryConfig &
  getRecoveryConfig() const {
    return recoveryConfig_;
  }
  const websocket_recovery::ConnectionRecoveryState &getRecoveryState() const {
    return recoveryState_;
  }
  void setErrorHandler(const ErrorHandler &handler) { errorHandler_ = handler; }

  // Connection health monitoring
  void startHeartbeat();
  void stopHeartbeat();
  void onHeartbeatReceived();
  bool isHeartbeatActive() const { return heartbeatActive_.load(); }
  std::chrono::system_clock::time_point getLastHeartbeat() const;

  // Connection filtering methods
  void setFilters(const ConnectionFilters &filters);
  const ConnectionFilters &getFilters() const { return filters_; }
  bool shouldReceiveMessage(MessageType type, const std::string &jobId = "",
                            const std::string &logLevel = "") const;
  bool shouldReceiveMessage(const WebSocketMessage &message) const;

  // Enhanced preference management
  void updateFilterPreferences(const ConnectionFilters &newFilters);
  void addJobIdFilter(const std::string &jobId);
  void removeJobIdFilter(const std::string &jobId);
  void addMessageTypeFilter(MessageType messageType);
  void removeMessageTypeFilter(MessageType messageType);
  void addLogLevelFilter(const std::string &logLevel);
  void removeLogLevelFilter(const std::string &logLevel);
  void clearFilters();

  // Filter statistics and information
  size_t getFilteredJobCount() const;
  size_t getFilteredMessageTypeCount() const;
  size_t getFilteredLogLevelCount() const;
  std::vector<std::string> getActiveJobFilters() const;
  std::vector<MessageType> getActiveMessageTypeFilters() const;
  std::vector<std::string> getActiveLogLevelFilters() const;

private:
  websocket::stream<tcp::socket> ws_;
  std::weak_ptr<WebSocketManager> manager_;
  std::string connectionId_;
  beast::flat_buffer buffer_;
  std::queue<std::string> messageQueue_;
  std::mutex queueMutex_;
  std::atomic<bool> isOpen_{false};
  std::atomic<bool> isWriting_{false};
  ConnectionFilters filters_;
  mutable std::mutex filtersMutex_;

  // Error handling and recovery
  websocket_recovery::ConnectionRecoveryConfig recoveryConfig_;
  websocket_recovery::ConnectionRecoveryState recoveryState_;
  websocket_recovery::ConnectionCircuitBreaker circuitBreaker_;
  ErrorHandler errorHandler_;

  // Heartbeat monitoring
  std::unique_ptr<boost::asio::steady_timer> heartbeatTimer_;
  std::atomic<bool> heartbeatActive_{false};
  std::chrono::system_clock::time_point lastHeartbeat_;
  mutable std::mutex heartbeatMutex_;

  void onAccept(beast::error_code ec);
  void doRead();
  void onRead(beast::error_code ec, std::size_t bytes_transferred);
  void doWrite();
  void onWrite(beast::error_code ec, std::size_t bytes_transferred);
  void doClose();

  // Error handling methods
  void handleError(const std::string &operation, beast::error_code ec);
  void attemptRecovery();
  bool shouldAttemptRecovery(beast::error_code ec);
  void sendPendingMessages();

  // Heartbeat methods
  void scheduleHeartbeat();
  void onHeartbeatTimer(beast::error_code ec);
  void sendHeartbeat();
  void checkHeartbeatTimeout();

  std::string generateConnectionId();
};