#pragma once

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class RequestHandler;
class WebSocketManager;
class ETLJobManager;
class JobMonitorService;
class ConnectionPoolManager;
class TimeoutManager;
struct ServerConfig;

class HttpServer {
public:
  HttpServer(const std::string &address, unsigned short port, int threads = 1);
  HttpServer(const std::string &address, unsigned short port, int threads, const ServerConfig& config);
  ~HttpServer();

  void start();
  void stop();
  bool isRunning() const;

  void setRequestHandler(std::shared_ptr<RequestHandler> handler);
  void setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager);

  // Configuration management
  void setServerConfig(const ServerConfig& config);
  ServerConfig getServerConfig() const;

  // Connection pool management
  std::shared_ptr<ConnectionPoolManager> getConnectionPoolManager();
  std::shared_ptr<TimeoutManager> getTimeoutManager();

  // Add getters for testing purposes
  std::shared_ptr<ETLJobManager> getJobManager();
  std::shared_ptr<JobMonitorService> getJobMonitorService();

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};
