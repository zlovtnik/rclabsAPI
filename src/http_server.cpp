#include "logger.hpp"
#include "http_server.hpp"
#include "request_handler.hpp"
#include "websocket_manager.hpp"
#include "etl_exceptions.hpp"
#include "connection_pool_manager.hpp"
#include "timeout_manager.hpp"
#include "pooled_session.hpp"
#include "server_config.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;



class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint, 
             std::shared_ptr<ConnectionPoolManager> poolManager)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc))
        , poolManager_(poolManager) {
        beast::error_code ec;

        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            fail(ec, "open");
            return;
        }

        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            fail(ec, "set_option");
            return;
        }

        acceptor_.bind(endpoint, ec);
        if (ec) {
            fail(ec, "bind");
            return;
        }

        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            fail(ec, "listen");
            return;
        }
    }

    void run() {
        doAccept();
    }

private:
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<ConnectionPoolManager> poolManager_;

    void fail(beast::error_code ec, char const* what) {
        std::cerr << what << ": " << ec.message() << "\n";
    }

    void doAccept() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(&Listener::onAccept, shared_from_this()));
    }

    void onAccept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            HTTP_LOG_ERROR("Listener::onAccept() - Error: " + ec.message());
            fail(ec, "accept");
            
            // Check if this is a fatal error that should stop accepting
            if (ec == boost::asio::error::operation_aborted ||
                ec == boost::asio::error::bad_descriptor) {
                HTTP_LOG_ERROR("Listener::onAccept() - Fatal error, stopping listener");
                return;
            }
            
            // For other errors (like EINVAL), continue accepting
            HTTP_LOG_WARN("Listener::onAccept() - Non-fatal error, continuing to accept connections");
        } else {
            HTTP_LOG_INFO("Listener::onAccept() - New connection accepted");
            if (!poolManager_) {
                HTTP_LOG_ERROR("Listener::onAccept() - ConnectionPoolManager is null!");
            } else {
                HTTP_LOG_DEBUG("Listener::onAccept() - Acquiring connection from pool");
                try {
                    auto session = poolManager_->acquireConnection(std::move(socket));
                    if (session) {
                        session->run();
                    } else {
                        HTTP_LOG_ERROR("Listener::onAccept() - Failed to acquire connection from pool");
                    }
                } catch (const std::exception& e) {
                    HTTP_LOG_ERROR("Listener::onAccept() - Exception acquiring connection: " + std::string(e.what()));
                }
            }
        }

        // Continue accepting connections regardless of whether the previous accept succeeded or failed
        // (unless it was a fatal error)
        doAccept();
    }
};

struct HttpServer::Impl {
    std::string address;
    unsigned short port;
    int threads;
    ServerConfig config;
    std::shared_ptr<RequestHandler> handler;
    std::shared_ptr<WebSocketManager> wsManager;
    std::shared_ptr<ConnectionPoolManager> poolManager;
    std::shared_ptr<TimeoutManager> timeoutManager;
    std::unique_ptr<net::io_context> ioc;
    std::vector<std::thread> threadPool;
    bool running = false;
};

HttpServer::HttpServer(const std::string& address, unsigned short port, int threads)
    : pImpl(std::make_unique<Impl>()) {
    pImpl->address = address;
    pImpl->port = port;
    pImpl->threads = std::max<int>(1, threads);
    pImpl->config = ServerConfig::create(); // Use default configuration
}

HttpServer::HttpServer(const std::string& address, unsigned short port, int threads, const ServerConfig& config)
    : pImpl(std::make_unique<Impl>()) {
    pImpl->address = address;
    pImpl->port = port;
    pImpl->threads = std::max<int>(1, threads);
    pImpl->config = config;
    
    // Validate and apply defaults to the configuration
    auto validation = pImpl->config.validate();
    if (!validation.isValid) {
        HTTP_LOG_ERROR("HttpServer constructor - Invalid configuration:");
        for (const auto& error : validation.errors) {
            HTTP_LOG_ERROR("  - " + error);
        }
        pImpl->config.applyDefaults();
        HTTP_LOG_INFO("HttpServer constructor - Applied default values for invalid configuration");
    }
    
    if (!validation.warnings.empty()) {
        HTTP_LOG_WARN("HttpServer constructor - Configuration warnings:");
        for (const auto& warning : validation.warnings) {
            HTTP_LOG_WARN("  - " + warning);
        }
    }
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    HTTP_LOG_INFO("HttpServer::start() - Starting HTTP server on " + pImpl->address + ":" + std::to_string(pImpl->port));
    
    if (pImpl->running) {
        HTTP_LOG_WARN("HttpServer::start() - Server already running");
        return;
    }

    if (!pImpl->handler) {
        HTTP_LOG_ERROR("HttpServer::start() - No request handler set!");
        throw etl::SystemException(etl::ErrorCode::CONFIGURATION_ERROR, 
                                   "No request handler set", 
                                   "HttpServer");
    }

    try {
        HTTP_LOG_DEBUG("HttpServer::start() - Creating IO context with " + std::to_string(pImpl->threads) + " threads");
        pImpl->ioc = std::make_unique<net::io_context>(pImpl->threads);
        
        // Initialize TimeoutManager
        HTTP_LOG_DEBUG("HttpServer::start() - Creating TimeoutManager");
        pImpl->timeoutManager = std::make_shared<TimeoutManager>(
            *pImpl->ioc,
            pImpl->config.connectionTimeout,
            pImpl->config.requestTimeout
        );
        
        // Initialize ConnectionPoolManager
        HTTP_LOG_DEBUG("HttpServer::start() - Creating ConnectionPoolManager");
        pImpl->poolManager = std::make_shared<ConnectionPoolManager>(
            *pImpl->ioc,
            pImpl->config.minConnections,
            pImpl->config.maxConnections,
            pImpl->config.idleTimeout,
            pImpl->handler,
            pImpl->wsManager,
            pImpl->timeoutManager,
            ConnectionPoolManager::MonitorConfig{nullptr},  // performanceMonitor
            ConnectionPoolManager::QueueConfig{pImpl->config.maxQueueSize, pImpl->config.maxQueueWaitTime}
        );
        
        // Start the cleanup timer for the connection pool
        pImpl->poolManager->startCleanupTimer();
        
        auto const address = net::ip::make_address(pImpl->address);
        HTTP_LOG_DEBUG("HttpServer::start() - Address parsed: " + address.to_string());
        
        HTTP_LOG_DEBUG("HttpServer::start() - Creating listener with connection pool");
        std::make_shared<Listener>(*pImpl->ioc, tcp::endpoint{address, pImpl->port}, pImpl->poolManager)->run();

        HTTP_LOG_DEBUG("HttpServer::start() - Starting thread pool");
        pImpl->threadPool.reserve(pImpl->threads);
        for (int i = 0; i < pImpl->threads; ++i) {
            pImpl->threadPool.emplace_back([this, i]() {
                HTTP_LOG_DEBUG("HttpServer thread " + std::to_string(i) + " starting");
                try {
                    pImpl->ioc->run();
                    HTTP_LOG_DEBUG("HttpServer thread " + std::to_string(i) + " finished");
                } catch (const std::exception& e) {
                    HTTP_LOG_ERROR("HttpServer thread " + std::to_string(i) + " exception: " + std::string(e.what()));
                }
            });
        }

        pImpl->running = true;
        HTTP_LOG_INFO("HttpServer::start() - HTTP server started successfully with connection pooling");
        HTTP_LOG_INFO("HttpServer::start() - Pool config: min=" + std::to_string(pImpl->config.minConnections) + 
                     ", max=" + std::to_string(pImpl->config.maxConnections) + 
                     ", idleTimeout=" + std::to_string(pImpl->config.idleTimeout.count()) + "s");
    } catch (const std::exception& e) {
        HTTP_LOG_ERROR("HttpServer::start() - Error starting server: " + std::string(e.what()));
        std::cerr << "Error starting server: " << e.what() << std::endl;
        throw;
    }
}

void HttpServer::stop() {
    if (!pImpl->running) {
        return;
    }

    HTTP_LOG_INFO("HttpServer::stop() - Stopping HTTP server");

    // Stop the connection pool cleanup timer first
    if (pImpl->poolManager) {
        HTTP_LOG_DEBUG("HttpServer::stop() - Stopping connection pool cleanup timer");
        pImpl->poolManager->stopCleanupTimer();
    }

    // Stop the IO context
    pImpl->ioc->stop();

    // Wait for all threads to finish
    for (auto& t : pImpl->threadPool) {
        if (t.joinable()) {
            t.join();
        }
    }

    pImpl->threadPool.clear();

    // Shutdown the connection pool
    if (pImpl->poolManager) {
        HTTP_LOG_DEBUG("HttpServer::stop() - Shutting down connection pool");
        pImpl->poolManager->shutdown();
    }

    // Cancel all timeouts
    if (pImpl->timeoutManager) {
        HTTP_LOG_DEBUG("HttpServer::stop() - Canceling all timeouts");
        pImpl->timeoutManager->cancelAllTimers();
    }

    pImpl->running = false;
    HTTP_LOG_INFO("HttpServer::stop() - HTTP server stopped successfully");
}

bool HttpServer::isRunning() const {
    return pImpl->running;
}

void HttpServer::setRequestHandler(std::shared_ptr<RequestHandler> handler) {
    HTTP_LOG_INFO("HttpServer::setRequestHandler() - Setting request handler: " + std::string(handler ? "valid" : "null"));
    pImpl->handler = handler;
    
    // If the pool manager already exists, we need to recreate it with the new handler
    if (pImpl->poolManager && pImpl->ioc && pImpl->timeoutManager) {
        HTTP_LOG_DEBUG("HttpServer::setRequestHandler() - Recreating connection pool with new handler");
        pImpl->poolManager->shutdown();
        pImpl->poolManager = std::make_shared<ConnectionPoolManager>(
            *pImpl->ioc,
            pImpl->config.minConnections,
            pImpl->config.maxConnections,
            pImpl->config.idleTimeout,
            pImpl->handler,
            pImpl->wsManager,
            pImpl->timeoutManager,
            ConnectionPoolManager::MonitorConfig{nullptr},  // performanceMonitor
            ConnectionPoolManager::QueueConfig{pImpl->config.maxQueueSize, pImpl->config.maxQueueWaitTime}
        );
        if (pImpl->running) {
            pImpl->poolManager->startCleanupTimer();
        }
    }
}

void HttpServer::setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager) {
    pImpl->wsManager = wsManager;
    
    // If the pool manager already exists, we need to recreate it with the new WebSocket manager
    if (pImpl->poolManager && pImpl->ioc && pImpl->timeoutManager) {
        HTTP_LOG_DEBUG("HttpServer::setWebSocketManager() - Recreating connection pool with new WebSocket manager");
        pImpl->poolManager->shutdown();
        pImpl->poolManager = std::make_shared<ConnectionPoolManager>(
            *pImpl->ioc,
            pImpl->config.minConnections,
            pImpl->config.maxConnections,
            pImpl->config.idleTimeout,
            pImpl->handler,
            pImpl->wsManager,
            pImpl->timeoutManager,
            ConnectionPoolManager::MonitorConfig{nullptr},  // performanceMonitor
            ConnectionPoolManager::QueueConfig{pImpl->config.maxQueueSize, pImpl->config.maxQueueWaitTime}
        );
        if (pImpl->running) {
            pImpl->poolManager->startCleanupTimer();
        }
    }
}

void HttpServer::setServerConfig(const ServerConfig& config) {
    pImpl->config = config;
    
    // Validate and apply defaults
    auto validation = pImpl->config.validate();
    if (!validation.isValid) {
        HTTP_LOG_ERROR("HttpServer::setServerConfig() - Invalid configuration:");
        for (const auto& error : validation.errors) {
            HTTP_LOG_ERROR("  - " + error);
        }
        pImpl->config.applyDefaults();
        HTTP_LOG_INFO("HttpServer::setServerConfig() - Applied default values for invalid configuration");
    }
    
    if (!validation.warnings.empty()) {
        HTTP_LOG_WARN("HttpServer::setServerConfig() - Configuration warnings:");
        for (const auto& warning : validation.warnings) {
            HTTP_LOG_WARN("  - " + warning);
        }
    }
    
    // Update timeout manager if it exists
    if (pImpl->timeoutManager) {
        pImpl->timeoutManager->setConnectionTimeout(pImpl->config.connectionTimeout);
        pImpl->timeoutManager->setRequestTimeout(pImpl->config.requestTimeout);
    }
    
    HTTP_LOG_INFO("HttpServer::setServerConfig() - Configuration updated");
}

ServerConfig HttpServer::getServerConfig() const {
    return pImpl->config;
}

std::shared_ptr<ConnectionPoolManager> HttpServer::getConnectionPoolManager() {
    return pImpl->poolManager;
}

std::shared_ptr<TimeoutManager> HttpServer::getTimeoutManager() {
    return pImpl->timeoutManager;
}

std::shared_ptr<ETLJobManager> HttpServer::getJobManager() {
    if (pImpl && pImpl->handler) {
        return pImpl->handler->getJobManager();
    }
    return nullptr;
}

std::shared_ptr<JobMonitorService> HttpServer::getJobMonitorService() {
    if (pImpl && pImpl->handler) {
        return pImpl->handler->getJobMonitorService();
    }
    return nullptr;
}
