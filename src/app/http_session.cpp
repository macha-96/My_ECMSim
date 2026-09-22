#include <app/http_session.h>
#include <app/router.h>
#include <app/ws_session.h>
#include <app/common.h>
#include <spdlog/spdlog.h>

// http_session 实现
http_session::http_session(tcp::socket socket) : socket_(std::move(socket)) {}

void http_session::start() { do_read(); }

bool http_session::isWebSocketUpgrade(const http::request<http::string_body>& req) {
    auto const& h = req.base();
    auto it = h.find(http::field::connection);
    if (it == h.end()) return false;
    if (it->value().find("Upgrade") == std::string::npos) return false;
    it = h.find(http::field::upgrade);
    if (it == h.end()) return false;
    return it->value() == "websocket";
}

std::string http_session::extractSessionId(const std::string& target) {
    size_t qpos = target.find('?');
    if (qpos == std::string::npos) return "";
    std::string qs = target.substr(qpos + 1);
    size_t pos = qs.find("session_id=");
    if (pos == std::string::npos) return "";
    std::string val = qs.substr(pos + 11);
    size_t amp = val.find('&');
    if (amp != std::string::npos) val = val.substr(0, amp);
    return val;
}

void http_session::do_read() {
    http::async_read(socket_, buffer_, req_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) return;
            self->process();
        });
}

void http_session::process() {
    std::string target = svToString(req_.target());

    if (isWebSocketUpgrade(req_)) {
        std::string sid = extractSessionId(target);
        if (!sid.empty()) {
            auto ws = std::make_shared<websocket_session>(std::move(socket_), std::move(req_), sid);
            ws->start();
            spdlog::info("websocket_session {} has been created!", sid);
        }
        return;
    }

    extern RouteTrie g_router;
    std::string response_str = g_router.dispatch(req_);
    bool is_html = (target == "/");

    http::response<http::string_body> res{http::status::ok, req_.version()};
    res.set(http::field::server, "ECMSim-beast");
    res.set(http::field::content_type, is_html ? "text/html" : "application/json");
    res.keep_alive(req_.keep_alive());
    res.body() = response_str;
    res.prepare_payload();

    auto res_shared = std::make_shared<http::response<http::string_body>>(std::move(res));
    http::async_write(socket_, *res_shared,
        [self = shared_from_this(), res_shared](beast::error_code ec, std::size_t) {
            if (ec) return;
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
        });
}

// http_server 实现
http_server::http_server(net::io_context& ioc, tcp::endpoint endpoint) : acceptor_(ioc) {
    beast::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) { spdlog::error("打开socket失败: {}", ec.message()); return; }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) { spdlog::error("设置socket选项失败: {}", ec.message()); return; }

    acceptor_.bind(endpoint, ec);
    if (ec) { spdlog::error("绑定端口失败: {}", ec.message()); return; }

    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) { spdlog::error("监听端口失败: {}", ec.message()); return; }

    do_accept();
}

void http_server::do_accept() {
    acceptor_.async_accept(
        [this](beast::error_code ec, tcp::socket socket) {
            if (ec) { do_accept(); return; }
            std::make_shared<http_session>(std::move(socket))->start();
            do_accept();
        });
}
