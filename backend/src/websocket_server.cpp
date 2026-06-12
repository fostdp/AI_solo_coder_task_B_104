#include "websocket_server.h"
#include <iostream>

namespace porcelain_monitor {

void WebSocketSession::run(http::request<http::string_body> req) {
    ws_.set_option(websocket::stream_base::timeout::suggested(
        beast::role_type::server));

    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " porcelain-monitor-websocket");
        }));

    ws_.async_accept(
        req,
        beast::bind_front_handler(
            &WebSocketSession::on_accept,
            shared_from_this()));
}

void WebSocketSession::on_accept(beast::error_code ec) {
    if (ec) {
        std::cerr << "WebSocket accept error: " << ec.message() << std::endl;
        return;
    }
    std::cout << "WebSocket connection accepted" << std::endl;
    do_read();
}

void WebSocketSession::do_read() {
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &WebSocketSession::on_read,
            shared_from_this()));
}

void WebSocketSession::on_read(beast::error_code ec, std::size_t) {
    if (ec == websocket::error::closed) {
        std::cout << "WebSocket connection closed" << std::endl;
        return;
    }

    if (ec) {
        std::cerr << "WebSocket read error: " << ec.message() << std::endl;
        return;
    }

    buffer_.consume(buffer_.size());
    do_read();
}

void WebSocketSession::on_write(beast::error_code ec, std::size_t) {
    if (ec) {
        std::cerr << "WebSocket write error: " << ec.message() << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    send_queue_.pop();

    if (!send_queue_.empty()) {
        do_send(send_queue_.front());
    }
}

void WebSocketSession::send(const std::string& message) {
    post(
        ws_.get_executor(),
        beast::bind_front_handler(
            &WebSocketSession::do_send,
            shared_from_this(),
            message));
}

void WebSocketSession::send_json(const nlohmann::json& j) {
    send(j.dump());
}

void WebSocketSession::do_send(std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);

    bool was_empty = send_queue_.empty();
    send_queue_.push(std::move(message));

    if (was_empty) {
        ws_.text(true);
        ws_.async_write(
            asio::buffer(send_queue_.front()),
            beast::bind_front_handler(
                &WebSocketSession::on_write,
                shared_from_this()));
    }
}

WebSocketServer::WebSocketServer(asio::io_context& ioc, uint16_t port)
    : ioc_(ioc),
      acceptor_(ioc, {tcp::v4(), port}) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start() {
    do_accept();
    std::cout << "WebSocket server started on port "
              << acceptor_.local_endpoint().port() << std::endl;
}

void WebSocketServer::stop() {
    beast::error_code ec;
    acceptor_.close(ec);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.clear();
}

void WebSocketServer::do_accept() {
    acceptor_.async_accept(
        asio::make_strand(ioc_),
        beast::bind_front_handler(
            &WebSocketServer::handle_accept,
            shared_from_this()));
}

void WebSocketServer::handle_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        std::cerr << "WebSocket accept error: " << ec.message() << std::endl;
    } else {
        auto session = std::make_shared<WebSocketSession>(std::move(socket));
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.push_back(session);
        }

        async_read(
            session->ws_,
            session->buffer_,
            [session](beast::error_code ec, std::size_t) {
                if (!ec) {
                    session->buffer_.consume(session->buffer_.size());
                }
            });
    }

    do_accept();
}

void WebSocketServer::handle_http_request(tcp::socket socket,
                                           http::request<http::string_body> req) {
    if (websocket::is_upgrade(req)) {
        auto session = std::make_shared<WebSocketSession>(std::move(socket));
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.push_back(session);
        }
        session->run(std::move(req));
    }
}

void WebSocketServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        session->send(message);
    }
}

void WebSocketServer::broadcast_json(const nlohmann::json& j) {
    broadcast(j.dump());
}

}
