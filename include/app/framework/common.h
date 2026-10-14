#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <sence/scene_manager.h>
#include <jsoncpp/json/json.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_map>

namespace beast = boost::beast;
namespace http = beast::http;

extern ECMSim::SceneManager g_scene_mgr;
extern std::string g_index_html;

class ws_broadcaster;
extern ws_broadcaster g_broadcaster;

// 静态文件缓存: 路径("/css/style.css") → 文件内容
extern std::unordered_map<std::string, std::string> g_static_cache;
// MIME 类型映射: 扩展名("css") → Content-Type
extern std::unordered_map<std::string, std::string> g_mime_types;

std::string svToString(beast::string_view sv);
std::string makeJson(const Json::Value& v);
std::string jsonOk();
std::string jsonErr(const std::string& msg);
Json::Value parseJson(const std::string& str);
std::string sceneToJson(const std::string& sid);
void broadcastScene(const std::string& sid);
bool loadFile(const std::string& p, std::string& out);
bool loadStaticDir(const std::string& dir);
std::string getMimeType(const std::string& path);
bool isPathSafe(const std::string& path);

class cleanup_controller {
public:
    cleanup_controller();
    void start();
    void stop();
    ~cleanup_controller();

private:
    void run_cleanup_loop();
    std::atomic<bool> running_;
    std::thread cleanup_thread_;
};
