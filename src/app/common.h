#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <sence/scene_manager.h>
#include <jsoncpp/json/json.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;

// 全局状态
extern ECMSim::SceneManager g_scene_mgr;
extern std::string g_index_html;

// 字符串工具
std::string svToString(beast::string_view sv);

// JSON 工具
std::string makeJson(const Json::Value& v);
std::string jsonOk();
std::string jsonErr(const std::string& msg);
Json::Value parseJson(const std::string& str);

// 场景工具
std::string sceneToJson(const std::string& sid);
void broadcastScene(const std::string& sid);

// 文件工具
bool loadFile(const std::string& p, std::string& out);

// 会话资源回收控制器
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
