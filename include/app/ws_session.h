#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <string>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <deque>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class websocket_session : public std::enable_shared_from_this<websocket_session> {
public:
    websocket_session(tcp::socket socket, http::request<http::string_body> req, const std::string& sid);
    void start();
    void send(const std::string& msg);

private:
    void do_write();
    void do_read();
    void on_close();

    websocket::stream<tcp::socket> ws_;
    http::request<http::string_body> req_;
    std::string sid_;
    beast::flat_buffer buffer_;
    std::deque<std::string> write_queue_;
    bool writing_;
};

class ws_broadcaster {
public:
    void subscribe(const std::string& sid, websocket_session* ws);
    void unsubscribe(const std::string& sid, websocket_session* ws);
    void broadcast(const std::string& sid, const std::string& msg);
    void cleanupStaleConnections();
    size_t countConnections() const;
    size_t countSessionConnections(const std::string& sid) const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::set<websocket_session*>> sessions_;
};
