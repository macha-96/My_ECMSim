#include <app/ws_session.h>
#include <app/common.h>
#include <spdlog/spdlog.h>

// websocket_session 实现
websocket_session::websocket_session(tcp::socket socket, http::request<http::string_body> req, const std::string& sid)
    : ws_(std::move(socket)), req_(std::move(req)), sid_(sid), writing_(false) {}

void websocket_session::start() {
    ws_.async_accept(req_,
        [self = shared_from_this()](beast::error_code ec) {
            if (ec) return;
            g_broadcaster.subscribe(self->sid_, self.get());
            self->do_read();
        });
}

void websocket_session::send(const std::string& msg) {
    boost::asio::post(ws_.get_executor(),
        [self = shared_from_this(), msg]() {
            self->write_queue_.push_back(msg);
            if (!self->writing_) {
                self->writing_ = true;
                self->do_write();
            }
        });
}

void websocket_session::do_write() {
    auto buf = std::make_shared<std::string>(write_queue_.front());
    ws_.async_write(net::buffer(*buf),
        [self = shared_from_this(), buf](beast::error_code ec, std::size_t) {
            if (ec) {
                self->on_close();
                return;
            }
            self->write_queue_.pop_front();
            if (self->write_queue_.empty()) {
                self->writing_ = false;
            } else {
                self->do_write();
            }
        });
}

void websocket_session::do_read() {
    ws_.async_read(buffer_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) { self->on_close(); return; }
            self->buffer_.consume(self->buffer_.size());
            self->do_read();
        });
}

void websocket_session::on_close() {
    g_broadcaster.unsubscribe(sid_, this);
}

// ws_broadcaster 实现
void ws_broadcaster::subscribe(const std::string& sid, websocket_session* ws) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[sid].insert(ws);
}

void ws_broadcaster::unsubscribe(const std::string& sid, websocket_session* ws) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return;
    it->second.erase(ws);
    if (it->second.empty()) sessions_.erase(it);
}

void ws_broadcaster::broadcast(const std::string& sid, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return;
    for (auto* ws : it->second) {
        ws->send(msg);
    }
}

void ws_broadcaster::cleanupStaleConnections() {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.begin();
    while (it != sessions_.end()) {
        if (it->second.empty()) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t ws_broadcaster::countConnections() const {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t total = 0;
    for (const auto& [sid, ws_set] : sessions_) {
        total += ws_set.size();
    }
    return total;
}

size_t ws_broadcaster::countSessionConnections(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return 0;
    return it->second.size();
}
