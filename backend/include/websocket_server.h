#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include <vector>
#include <mutex>
#include <queue>
#include <string>
#include <nlohmann/json.hpp>

namespace porcelain_monitor {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(tcp::socket socket) : ws_(std::move(socket)) {}

    void run(http::request<http::string_body> req);
    void send(const std::string& message);
    void send_json(const nlohmann::json& j);

private:
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void do_send(std::string message);

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::queue<std::string> send_queue_;
    std::mutex mutex_;
};

class WebSocketServer : public std::enable_shared_from_this<WebSocketServer> {
public:
    WebSocketServer(asio::io_context& ioc, uint16_t port);
    ~WebSocketServer();

    void start();
    void stop();
    void broadcast(const std::string& message);
    void broadcast_json(const nlohmann::json& j);

private:
    void do_accept();
    void handle_accept(beast::error_code ec, tcp::socket socket);
    void handle_http_request(tcp::socket socket,
                             http::request<http::string_body> req);

    asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<WebSocketSession>> sessions_;
    std::mutex sessions_mutex_;
};

}
