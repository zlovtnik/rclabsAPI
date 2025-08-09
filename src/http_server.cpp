#include "logger.hpp"
#include "http_server.hpp"
#include "request_handler.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
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

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket&& socket, std::shared_ptr<RequestHandler> handler)
        : stream_(std::move(socket))
        , handler_(handler) {
        HTTP_LOG_DEBUG("Session created with handler: " + std::string(handler ? "valid" : "null"));
    }

    void run() {
        HTTP_LOG_DEBUG("Session::run() - Starting session");
        net::dispatch(stream_.get_executor(),
                     beast::bind_front_handler(&Session::doRead, shared_from_this()));
    }

private:
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<RequestHandler> handler_;

    void doRead() {
        HTTP_LOG_DEBUG("Session::doRead() - Starting read operation");
        req_ = {};
        stream_.expires_after(std::chrono::seconds(30));

        // Set a reasonable limit for request body size (10MB)
        auto parser = std::make_shared<http::request_parser<http::string_body>>();
        parser->body_limit(10 * 1024 * 1024); // 10MB limit
        
        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(&Session::onRead, shared_from_this()));
    }

    void onRead(beast::error_code ec, std::size_t bytes_transferred) {
        HTTP_LOG_DEBUG("Session::onRead() - Read completed, bytes: " + std::to_string(bytes_transferred));
        boost::ignore_unused(bytes_transferred);

        if (ec == http::error::end_of_stream) {
            HTTP_LOG_DEBUG("Session::onRead() - End of stream, closing");
            return doClose();
        }

        if (ec) {
            HTTP_LOG_ERROR("Session::onRead() - Error: " + ec.message());
            return;
        }

        HTTP_LOG_INFO("Session::onRead() - Processing request: " + std::string(req_.method_string()) + " " + std::string(req_.target()));
        
        if (!handler_) {
            HTTP_LOG_ERROR("Session::onRead() - Handler is null!");
            // Send a proper error response instead of just returning
            http::response<http::string_body> error_res{http::status::internal_server_error, req_.version()};
            error_res.set(http::field::server, "ETL Plus Backend");
            error_res.set(http::field::content_type, "application/json");
            error_res.keep_alive(false);
            error_res.body() = "{\"error\":\"Internal server error - handler not available\"}";
            error_res.prepare_payload();
            sendResponse(std::move(error_res));
            return;
        }
        
        try {
            HTTP_LOG_DEBUG("Session::onRead() - Calling handler->handleRequest()");
            auto response = handler_->handleRequest(std::move(req_));
            HTTP_LOG_DEBUG("Session::onRead() - Handler completed, sending response");
            sendResponse(std::move(response));
        } catch (const std::exception& e) {
            HTTP_LOG_ERROR("Session::onRead() - Exception in handler: " + std::string(e.what()));
            // Send proper error response for exceptions
            http::response<http::string_body> error_res{http::status::internal_server_error, req_.version()};
            error_res.set(http::field::server, "ETL Plus Backend");
            error_res.set(http::field::content_type, "application/json");
            error_res.keep_alive(false);
            error_res.body() = "{\"error\":\"Internal server error\"}";
            error_res.prepare_payload();
            sendResponse(std::move(error_res));
        } catch (...) {
            HTTP_LOG_ERROR("Session::onRead() - Unknown exception in handler");
            // Send proper error response for unknown exceptions
            http::response<http::string_body> error_res{http::status::internal_server_error, req_.version()};
            error_res.set(http::field::server, "ETL Plus Backend");
            error_res.set(http::field::content_type, "application/json");
            error_res.keep_alive(false);
            error_res.body() = "{\"error\":\"Internal server error\"}";
            error_res.prepare_payload();
            sendResponse(std::move(error_res));
        }
    }

    void sendResponse(http::response<http::string_body>&& msg) {
        HTTP_LOG_DEBUG("Session::sendResponse() - Sending response with status: " + std::to_string(msg.result_int()));
        HTTP_LOG_DEBUG("Session::sendResponse() - Response body size: " + std::to_string(msg.body().size()));
        
        try {
            HTTP_LOG_DEBUG("Session::sendResponse() - Creating shared response object");
            auto response = std::make_shared<http::response<http::string_body>>(std::move(msg));
            HTTP_LOG_DEBUG("Session::sendResponse() - Shared response created successfully");
            
            // Ensure the session stays alive for the duration of the write operation
            auto self = shared_from_this();
            
            HTTP_LOG_DEBUG("Session::sendResponse() - About to call http::async_write");
            http::async_write(stream_, *response,
                [self, response](beast::error_code ec, std::size_t bytes_transferred) {
                    self->onWrite(response->need_eof(), ec, bytes_transferred);
                });
            HTTP_LOG_DEBUG("Session::sendResponse() - http::async_write called successfully");
        } catch (const std::exception& e) {
            HTTP_LOG_ERROR("Session::sendResponse() - Exception: " + std::string(e.what()));
            // Close the connection on error
            doClose();
        } catch (...) {
            HTTP_LOG_ERROR("Session::sendResponse() - Unknown exception occurred");
            // Close the connection on unknown error
            doClose();
        }
    }

    void onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred) {
        HTTP_LOG_DEBUG("Session::onWrite() - Write completed, bytes: " + std::to_string(bytes_transferred) + ", close: " + (close ? "true" : "false"));
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            HTTP_LOG_ERROR("Session::onWrite() - Error: " + ec.message());
            return;
        }

        if (close) {
            HTTP_LOG_DEBUG("Session::onWrite() - Closing connection");
            return doClose();
        }

        HTTP_LOG_DEBUG("Session::onWrite() - Continuing to read");
        doRead();
    }

    void doClose() {
        HTTP_LOG_DEBUG("Session::doClose() - Closing session");
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        if (ec) {
            HTTP_LOG_WARN("Session::doClose() - Shutdown error: " + ec.message());
        }
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint, std::shared_ptr<RequestHandler> handler)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc))
        , handler_(handler) {
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
    std::shared_ptr<RequestHandler> handler_;

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
        } else {
            HTTP_LOG_INFO("Listener::onAccept() - New connection accepted");
            if (!handler_) {
                HTTP_LOG_ERROR("Listener::onAccept() - Handler is null!");
            } else {
                HTTP_LOG_DEBUG("Listener::onAccept() - Creating session with valid handler");
                std::make_shared<Session>(std::move(socket), handler_)->run();
            }
        }

        doAccept();
    }
};

struct HttpServer::Impl {
    std::string address;
    unsigned short port;
    int threads;
    std::shared_ptr<RequestHandler> handler;
    std::unique_ptr<net::io_context> ioc;
    std::vector<std::thread> threadPool;
    bool running = false;
};

HttpServer::HttpServer(const std::string& address, unsigned short port, int threads)
    : pImpl(std::make_unique<Impl>()) {
    pImpl->address = address;
    pImpl->port = port;
    pImpl->threads = std::max<int>(1, threads);
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
        throw std::runtime_error("No request handler set");
    }

    try {
        HTTP_LOG_DEBUG("HttpServer::start() - Creating IO context with " + std::to_string(pImpl->threads) + " threads");
        pImpl->ioc = std::make_unique<net::io_context>(pImpl->threads);
        
        auto const address = net::ip::make_address(pImpl->address);
        HTTP_LOG_DEBUG("HttpServer::start() - Address parsed: " + address.to_string());
        
        HTTP_LOG_DEBUG("HttpServer::start() - Creating listener");
        std::make_shared<Listener>(*pImpl->ioc, tcp::endpoint{address, pImpl->port}, pImpl->handler)->run();

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
        HTTP_LOG_INFO("HttpServer::start() - HTTP server started successfully");
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

    pImpl->ioc->stop();

    for (auto& t : pImpl->threadPool) {
        if (t.joinable()) {
            t.join();
        }
    }

    pImpl->threadPool.clear();
    pImpl->running = false;
}

bool HttpServer::isRunning() const {
    return pImpl->running;
}

void HttpServer::setRequestHandler(std::shared_ptr<RequestHandler> handler) {
    HTTP_LOG_INFO("HttpServer::setRequestHandler() - Setting request handler: " + std::string(handler ? "valid" : "null"));
    pImpl->handler = handler;
}
