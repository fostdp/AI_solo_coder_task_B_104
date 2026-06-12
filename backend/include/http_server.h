#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "common.h"

namespace porcelain_monitor {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

using HttpHandler = std::function<http::response<http::string_body>(
    const http::request<http::string_body>&,
    const std::smatch&)>;

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(asio::io_context& ioc, uint16_t port, const std::string& doc_root);
    ~HttpServer();

    void start();
    void stop();

    void add_route(const std::string& pattern, http::verb method, HttpHandler handler);

private:
    struct Route {
        std::regex pattern;
        http::verb method;
        HttpHandler handler;
    };

    struct Connection : std::enable_shared_from_this<Connection> {
        Connection(tcp::socket socket, HttpServer& server)
            : socket_(std::move(socket)), server_(server) {}

        void start() { do_read(); }

        void do_read() {
            auto self(shared_from_this());
            http::async_read(socket_, buffer_, req_,
                [this, self](beast::error_code ec, std::size_t) {
                    if (!ec) {
                        server_.handle_request(std::move(socket_), std::move(req_));
                    }
                });
        }

        tcp::socket socket_;
        HttpServer& server_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;
    };

    void do_accept();
    void handle_accept(beast::error_code ec, tcp::socket socket);
    void handle_request(tcp::socket socket, http::request<http::string_body> req);

    http::response<http::string_body> route_request(const http::request<http::string_body>& req);
    http::response<http::string_body> serve_file(const std::string& path);
    http::response<http::string_body> not_found(const std::string& target);
    http::response<http::string_body> json_response(const nlohmann::json& j, unsigned status = 200);
    http::response<http::string_body> error_response(unsigned status, const std::string& message);

    void register_api_routes();

    asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::string doc_root_;
    std::vector<Route> routes_;
    std::mutex routes_mutex_;
};

}
