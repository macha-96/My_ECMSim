#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class http_session : public std::enable_shared_from_this<http_session> {
public:
    http_session(tcp::socket socket);
    void start();

private:
    void do_read();
    void process();

    static bool isWebSocketUpgrade(const http::request<http::string_body>& req);
    static std::string extractSessionId(const std::string& target);

    beast::flat_buffer buffer_;
    tcp::socket socket_;
    http::request<http::string_body> req_;
};

class http_server {
public:
    http_server(net::io_context& ioc, tcp::endpoint endpoint);

private:
    void do_accept();
    tcp::acceptor acceptor_;
};
