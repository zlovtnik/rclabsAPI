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

class HttpServer {
public:
  HttpServer(const std::string &address, unsigned short port, int threads = 1);
  ~HttpServer();

  void start();
  void stop();
  bool isRunning() const;

  void setRequestHandler(std::shared_ptr<RequestHandler> handler);
  void setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager);

  // Add getters for testing purposes
  std::shared_ptr<ETLJobManager> getJobManager();
  std::shared_ptr<JobMonitorService> getJobMonitorService();

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};
