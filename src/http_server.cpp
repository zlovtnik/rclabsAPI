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
    }

    void run() {
        net::dispatch(stream_.get_executor(),
                     beast::bind_front_handler(&Session::doRead, shared_from_this()));
    }

private:
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<RequestHandler> handler_;

    void doRead() {
        req_ = {};
        stream_.expires_after(std::chrono::seconds(30));

        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(&Session::onRead, shared_from_this()));
    }

    void onRead(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec == http::error::end_of_stream)
            return doClose();

        if (ec)
            return;

        sendResponse(handler_->handleRequest(std::move(req_)));
    }

    void sendResponse(http::response<http::string_body>&& msg) {
        auto response = std::make_shared<http::response<http::string_body>>(std::move(msg));

        http::async_write(stream_, *response,
            beast::bind_front_handler(&Session::onWrite, shared_from_this(), response->need_eof()));
    }

    void onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return;

        if (close) {
            return doClose();
        }

        doRead();
    }

    void doClose() {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
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
            fail(ec, "accept");
        } else {
            std::make_shared<Session>(std::move(socket), handler_)->run();
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
    if (pImpl->running) {
        return;
    }

    try {
        pImpl->ioc = std::make_unique<net::io_context>(pImpl->threads);
        
        auto const address = net::ip::make_address(pImpl->address);
        
        std::make_shared<Listener>(*pImpl->ioc, tcp::endpoint{address, pImpl->port}, pImpl->handler)->run();

        pImpl->threadPool.reserve(pImpl->threads);
        for (int i = 0; i < pImpl->threads; ++i) {
            pImpl->threadPool.emplace_back([this]() {
                pImpl->ioc->run();
            });
        }

        pImpl->running = true;
    } catch (const std::exception& e) {
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
    pImpl->handler = handler;
}
