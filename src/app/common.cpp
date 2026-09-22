#include <app/common.h>
#include <app/ws_session.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>

// 全局状态定义
ECMSim::SceneManager g_scene_mgr;
std::string g_index_html;
ws_broadcaster g_broadcaster;

// 字符串工具
std::string svToString(beast::string_view sv) {
    return std::string(sv.data(), sv.size());
}

// JSON 工具
std::string makeJson(const Json::Value& v) {
    Json::StyledWriter w;
    return w.write(v);
}

std::string jsonOk() {
    return "{\"success\":true}\n";
}

std::string jsonErr(const std::string& msg) {
    Json::Value r;
    r["success"] = false;
    r["error"] = msg;
    return makeJson(r);
}

Json::Value parseJson(const std::string& str) {
    Json::Value r;
    Json::Reader rd;
    rd.parse(str, r, false);
    return r;
}

// 场景工具
std::string sceneToJson(const std::string& sid) {
    Json::Value v;
    Json::Reader rd;
    rd.parse(g_scene_mgr.getSceneJson(sid), v);
    v["success"] = true;
    v["session_id"] = sid;
    return makeJson(v);
}

void broadcastScene(const std::string& sid) {
    g_broadcaster.broadcast(sid, sceneToJson(sid));
}

// 文件工具
bool loadFile(const std::string& p, std::string& out) {
    std::ifstream f(p);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

// 会话资源回收控制器
cleanup_controller::cleanup_controller() : running_(false) {}

void cleanup_controller::start() {
    if (running_) return;
    running_ = true;
    cleanup_thread_ = std::thread([this]() { run_cleanup_loop(); });
    spdlog::info("会话资源回收线程已启动 (每5分钟清理一次)");
}

void cleanup_controller::stop() {
    if (!running_) return;
    running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    spdlog::info("会话资源回收线程已停止");
}

cleanup_controller::~cleanup_controller() {
    stop();
}

void cleanup_controller::run_cleanup_loop() {
    using namespace std::chrono;

    while (running_) {
        std::this_thread::sleep_for(minutes(5));
        if (!running_) break;

        try {
            spdlog::info("开始定期清理...");

            size_t before = g_scene_mgr.getSessionCount();
            g_scene_mgr.cleanupStaleSessions(1800);
            size_t after = g_scene_mgr.getSessionCount();

            g_broadcaster.cleanupStaleConnections();

            spdlog::info("清理完成: 会话 {} → {}, WebSocket连接数: {}", before, after, g_broadcaster.countConnections());

        } catch (const std::exception& e) {
            spdlog::error("清理异常: {}", e.what());
        }
    }
}
