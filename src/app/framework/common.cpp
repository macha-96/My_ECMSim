#include <app/framework/common.h>
#include <app/framework/ws_session.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <algorithm>

// 全局状态定义
ECMSim::SceneManager g_scene_mgr;
std::string g_index_html;
ws_broadcaster g_broadcaster;
std::unordered_map<std::string, std::string> g_static_cache;
std::unordered_map<std::string, std::string> g_mime_types = {
    {"html", "text/html; charset=utf-8"},
    {"htm", "text/html; charset=utf-8"},
    {"css", "text/css; charset=utf-8"},
    {"js", "application/javascript; charset=utf-8"},
    {"json", "application/json; charset=utf-8"},
    {"png", "image/png"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"gif", "image/gif"},
    {"svg", "image/svg+xml"},
    {"ico", "image/x-icon"},
    {"woff", "font/woff"},
    {"woff2", "font/woff2"},
    {"ttf", "font/ttf"},
    {"eot", "application/vnd.ms-fontobject"},
    {"xml", "application/xml; charset=utf-8"},
    {"txt", "text/plain; charset=utf-8"},
    {"map", "application/json"},
    {"wasm", "application/wasm"}
};

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
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

static void scanDirRecursive(const std::filesystem::path& dir, const std::string& prefix) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        auto rel = prefix + "/" + entry.path().filename().string();
        if (entry.is_directory()) {
            scanDirRecursive(entry.path(), rel);
        } else if (entry.is_regular_file()) {
            std::ifstream f(entry.path(), std::ios::binary);
            if (!f.is_open()) continue;
            std::stringstream ss;
            ss << f.rdbuf();
            auto content = ss.str();
            if (!content.empty()) {
                g_static_cache[rel] = std::move(content);
                spdlog::debug("缓存静态文件: {} ({} bytes)", rel, g_static_cache[rel].size());
            }
        }
    }
}

bool loadStaticDir(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        spdlog::error("静态文件目录不存在: {}", dir);
        return false;
    }

    g_static_cache.clear();
    scanDirRecursive(dir, "");

    auto it = g_static_cache.find("/index.html");
    if (it != g_static_cache.end()) {
        g_index_html = it->second;
        spdlog::info("静态目录已加载: {} (共 {} 个文件, index.html 已缓存)",
                     dir, g_static_cache.size());
        return true;
    }

    spdlog::error("静态目录中未找到 index.html: {}", dir);
    return false;
}

std::string getMimeType(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = g_mime_types.find(ext);
    return it != g_mime_types.end() ? it->second : "application/octet-stream";
}

bool isPathSafe(const std::string& path) {
    if (path.find("..") != std::string::npos) return false;
    if (path.find('\0') != std::string::npos) return false;
    if (!path.empty() && path[0] != '/') return false;
    return true;
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
