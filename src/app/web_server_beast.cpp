/***
 * ECMSim HTTP+gRPC+WebSocket Server — boost.beast implementation
 *
 * 三端合一服务器架构:
 *   - HTTP  :8080  前端页面 + REST API
 *   - gRPC  :50051 Python DQN 智能体通信
 *   - WebSocket      后端→前端实时推送（gRPC/HTTP 修改场景时触发）
 *
 * HTTP REST API 路由:
 *   POST   /api/scene           创建会话
 *   GET    /api/scene           获取场景配置 (body: {session_id})
 *   DELETE /api/scene           删除会话
 *   GET    /api/radars          列出所有雷达
 *   POST   /api/radars          添加雷达
 *   GET    /api/radar           获取单个雷达
 *   PUT    /api/radar           更新雷达参数
 *   DELETE /api/radar           删除雷达
 *   GET    /api/jammers         列出所有干扰机
 *   POST   /api/jammers         添加干扰机
 *   GET    /api/jammer          获取单个干扰机
 *   PUT    /api/jammer          更新干扰机参数
 *   DELETE /api/jammer          删除干扰机
 *   POST   /api/simulate        运行仿真
 *   GET    /api/dqn/state       获取 DQN 状态向量
 *   POST   /api/dqn/action      执行 DQN 动作
 *   WS     /ws?session_id=xxx   WebSocket 实时同步
 *
 * 数据流:
 *   前端/Python → HTTP/gRPC → SceneManager 修改数据
 *                                ↓
 *                           broadcastScene()
 *                                ↓
 *                           WebSocket 推送到对应会话的前端
 */

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <sence/scene_manager.h>
#include <algo/math_const.h>
#include <jsoncpp/json/json.h>
#include <grpc_gen/agent_service.grpc.pb.h>
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <memory>
#include <vector>
#include <set>
#include <unordered_map>
#include <algorithm>

/* boost::beast / asio 命名空间别名 */
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/* ====== 全局状态 ====== */
static ECMSim::SceneManager g_scene_mgr;    /* 场景管理器（所有会话的实体数据） */
static std::string g_index_html;            /* 启动时加载的前端 HTML 页面 */

/* ====== 前向声明 ====== */
class websocket_session;
class ws_broadcaster;
static std::string svToString(beast::string_view sv);
std::string routeRequest(const http::request<http::string_body>& req);

/* ====== WebSocket 广播器 ======
 * 按 session_id 管理 WebSocket 连接，提供订阅/取消订阅/广播功能。
 * 当 gRPC 或 HTTP 修改场景数据时，调用 broadcast() 推送最新场景 JSON 给前端。
 *
 * 线程安全: 所有操作加互斥锁，因为 HTTP 和 gRPC 可能并发调用。
 *
 * 生命周期:
 *   - subscribe()   : WebSocket 握手成功后调用，注册连接
 *   - unsubscribe() : 连接关闭时调用，移除连接
 *   - broadcast()   : 场景变更时调用，向该会话所有前端推送更新
 */
class ws_broadcaster {
public:
    void subscribe(const std::string& sid, websocket_session* ws);
    void unsubscribe(const std::string& sid, websocket_session* ws);
    void broadcast(const std::string& sid, const std::string& msg);

private:
    std::mutex mtx_;
    /* 会话ID → 该会话的所有 WebSocket 连接集合 */
    std::unordered_map<std::string, std::set<websocket_session*>> sessions_;
};

/* 全局广播器实例（必须在 websocket_session 之前定义，因为 websocket_session 的
 * start()/on_close() 会直接引用它） */
ws_broadcaster g_broadcaster;

/* ====== WebSocket 会话 ======
 * 管理单个 WebSocket 连接的生命周期。
 *
 * 工作流程:
 *   1. HTTP 服务器检测到 WebSocket 升级请求，创建本对象
 *   2. start() 执行 WebSocket 握手 (async_accept)
 *   3. 握手成功后注册到 g_broadcaster，开始持续读取客户端消息
 *   4. 外部通过 send() 向客户端推送消息
 *   5. 连接关闭时自动从 g_broadcaster 注销
 *
 * 注意: 必须持有原始 HTTP 请求 (req_) 用于 WebSocket 握手响应。
 */
class websocket_session : public std::enable_shared_from_this<websocket_session> {
public:
    websocket_session(tcp::socket socket, http::request<http::string_body> req, const std::string& sid)
        : ws_(std::move(socket)), req_(std::move(req)), sid_(sid) {}

    /* 执行 WebSocket 握手，成功后注册到广播器并开始读取 */
    void start() {
        ws_.async_accept(req_,
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) return;
                g_broadcaster.subscribe(self->sid_, self.get());
                self->do_read();
            });
    }

    /* 异步发送消息给客户端 */
    void send(const std::string& msg) {
        auto buf = std::make_shared<std::string>(msg);
        ws_.async_write(net::buffer(*buf),
            [self = shared_from_this(), buf](beast::error_code ec, std::size_t) {
                if (ec) self->on_close();
            });
    }

private:
    /* 持续读取客户端消息（用于检测连接关闭） */
    void do_read() {
        ws_.async_read(buffer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) { self->on_close(); return; }
                self->buffer_.consume(self->buffer_.size());
                self->do_read();
            });
    }

    /* 连接关闭，从广播器注销 */
    void on_close() {
        g_broadcaster.unsubscribe(sid_, this);
    }

    websocket::stream<tcp::socket> ws_;             /* WebSocket 流 */
    http::request<http::string_body> req_;          /* 原始 HTTP 升级请求（握手需要） */
    std::string sid_;                               /* 所属会话 ID */
    beast::flat_buffer buffer_;                     /* 读取缓冲区 */
};

/* ====== ws_broadcaster 方法实现 ======
 * 定义在 websocket_session 之后，因为 broadcast() 需要调用
 * websocket_session::send()（需要完整类型）。 */

void ws_broadcaster::subscribe(const std::string& sid, websocket_session* ws) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[sid].insert(ws);
}

void ws_broadcaster::unsubscribe(const std::string& sid, websocket_session* ws) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return;
    it->second.erase(ws);
    /* 如果该会话已无连接，清理空条目 */
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

/* ====== 工具函数 ====== */

/* 从文件路径加载全部内容到字符串 */
static bool loadFile(const std::string& p, std::string& out) {
    std::ifstream f(p);
    if (!f.is_open()) {
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

/* Json::Value → 格式化 JSON 字符串 */
static std::string makeJson(const Json::Value& v) {
    Json::StyledWriter w;
    return w.write(v);
}

/* 快捷返回: {"success":true}\n */
static std::string jsonOk() {
    return "{\"success\":true}\n";
}

/* 快捷返回: {"success":false,"error":"<msg>"} */
static std::string jsonErr(const std::string& msg) {
    Json::Value r;
    r["success"] = false;
    r["error"] = msg;
    return makeJson(r);
}

/* 解析 JSON 字符串为 Json::Value */
static Json::Value parseJson(const std::string& str) {
    Json::Value r;
    Json::Reader rd;
    rd.parse(str, r, false);
    return r;
}

/* beast::string_view → std::string */
static std::string svToString(beast::string_view sv) {
    return std::string(sv.data(), sv.size());
}

/* 将指定会话的完整场景序列化为 JSON（含 radars + jammers） */
static std::string sceneToJson(const std::string& sid) {
    Json::Value v;
    Json::Reader rd;
    rd.parse(g_scene_mgr.getSceneJson(sid), v);
    v["success"] = true;
    v["session_id"] = sid;
    return makeJson(v);
}

/* 广播场景更新：获取场景 JSON → 推送给该会话的所有 WebSocket 客户端 */
static void broadcastScene(const std::string& sid) {
    g_broadcaster.broadcast(sid, sceneToJson(sid));
}

/* ====== HTTP 请求处理器 ======
 * 每个 handler 根据 HTTP 方法 (GET/POST/PUT/DELETE) 分发到对应逻辑。
 * 修改场景数据后调用 broadcastScene() 推送更新到前端。 */

/* /api/scene — 会话管理
 * POST (无action): 创建新会话，返回 session_id
 * POST action=config: 获取场景配置（雷达+干扰机列表）
 * POST action=delete: 删除会话
 * GET: 获取场景配置
 * DELETE: 删除会话 */
std::string handleScene(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid;
    std::string action;
    std::string resp;

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action.empty()) {
            /* 创建新会话 */
            sid = g_scene_mgr.createSession();
            Json::Value r;
            r["success"] = true;
            r["session_id"] = sid;
            resp = makeJson(r);
        } else if (action == "config") {
            /* 获取场景配置 */
            sid = body["session_id"].asString();
            if (sid.empty()) {
                resp = jsonErr("need session_id");
                goto send;
            }
            if (!g_scene_mgr.hasSession(sid)) {
                resp = jsonErr("session not found");
                goto send;
            }
            Json::Value v;
            Json::Reader rd;
            rd.parse(g_scene_mgr.getSceneJson(sid), v);
            v["success"] = true;
            v["session_id"] = sid;
            resp = makeJson(v);
        } else if (action == "delete") {
            /* 删除会话 */
            sid = body["session_id"].asString();
            if (sid.empty()) {
                resp = jsonErr("need session_id");
                goto send;
            }
            resp = g_scene_mgr.deleteSession(sid)
                       ? jsonOk()
                       : jsonErr("session not found");
        } else {
            resp = jsonErr("unknown action");
        }
    } else if (req.method() == http::verb::get) {
        /* 获取场景配置 */
        sid = body["session_id"].asString();
        if (sid.empty()) {
            resp = jsonErr("need session_id");
            goto send;
        }
        if (!g_scene_mgr.hasSession(sid)) {
            resp = jsonErr("session not found");
            goto send;
        }
        Json::Value v;
        Json::Reader rd;
        rd.parse(g_scene_mgr.getSceneJson(sid), v);
        v["success"] = true;
        v["session_id"] = sid;
        resp = makeJson(v);
    } else if (req.method() == http::verb::delete_) {
        /* 删除会话 */
        sid = body["session_id"].asString();
        if (sid.empty()) {
            resp = jsonErr("need session_id");
            goto send;
        }
        resp = g_scene_mgr.deleteSession(sid)
                   ? jsonOk()
                   : jsonErr("session not found");
    } else {
        resp = jsonErr("use POST/GET/DELETE");
    }
send:
    return resp;
}

/* /api/radars — 雷达批量操作
 * GET: 列出会话中所有雷达
 * POST: 添加一个新雷达 */
std::string handleRadars(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string resp;

    if (sid.empty()) {
        resp = jsonErr("need session_id");
        goto send;
    }
    if (!g_scene_mgr.hasSession(sid)) {
        resp = jsonErr("session not found");
        goto send;
    }

    if (req.method() == http::verb::get) {
        /* 遍历所有雷达，转换为 JSON 数组 */
        Json::Value arr(Json::arrayValue);
        for (int id : g_scene_mgr.getRadarIds(sid)) {
            auto* r = g_scene_mgr.getRadar(sid, id);
            if (!r) continue;
            Json::Value j;
            j["id"] = r->getId();
            j["x"] = r->getPos().first;
            j["y"] = r->getPos().second;
            /* 线性值转 dB 显示 */
            j["Pt_dBm"] = 10 * log10(r->getPtLin() * 1000);
            j["G_dB"] = 10 * log10(r->getGLin());
            j["freq"] = r->getFreq();
            j["bandwidth"] = r->getBandwidth();
            j["sigma"] = r->getRCS();
            j["thresh_db"] = 10 * log10(r->getThreshLin());
            arr.append(j);
        }
        Json::Value ret;
        ret["success"] = true;
        ret["radars"] = arr;
        resp = makeJson(ret);
    } else if (req.method() == http::verb::post) {
        /* 从 JSON 构造 Radar 对象并添加到场景 */
        auto jr = body["radar"];
        if (jr.isNull()) {
            resp = jsonErr("need radar object");
            goto send;
        }
        ECMSim::Radar rad(
            jr["id"].asInt(), jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        bool ok = g_scene_mgr.addRadar(sid, rad);
        if (ok) broadcastScene(sid);  /* 推送更新给前端 */
        resp = ok ? jsonOk() : jsonErr("add failed");
    } else {
        resp = jsonErr("use GET/POST");
    }
send:
    return resp;
}

/* /api/radar — 单个雷达操作
 * GET: 获取指定雷达参数
 * POST action=del: 删除雷达
 * POST action=update: 更新雷达参数
 * PUT: 更新雷达参数 */
std::string handleRadar(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string action;
    std::string resp;

    if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int rid = body["radar_id"].asInt();
            if (!rid) { resp = jsonErr("need radar_id"); goto send; }
            bool ok = g_scene_mgr.removeRadar(sid, rid);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("not found");
        } else if (action == "update") {
            auto jr = body["radar"];
            if (jr.isNull()) { resp = jsonErr("need radar"); goto send; }
            int rid = jr["id"].asInt();
            ECMSim::Radar rad(
                rid, jr["x"].asDouble(), jr["y"].asDouble(),
                jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
                jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
            bool ok = g_scene_mgr.updateRadar(sid, rid, rad);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("update failed");
        } else {
            resp = jsonErr("unknown action");
        }
    } else if (req.method() == http::verb::get || req.method() == http::verb::delete_) {
        int rid = body["radar_id"].asInt();
        if (!rid) { resp = jsonErr("need radar_id"); goto send; }
        if (req.method() == http::verb::get) {
            auto* r = g_scene_mgr.getRadar(sid, rid);
            if (!r) { resp = jsonErr("not found"); goto send; }
            Json::Value j;
            j["success"] = true;
            j["radar_id"] = rid;
            j["x"] = r->getPos().first;
            j["y"] = r->getPos().second;
            j["Pt_dBm"] = 10 * log10(r->getPtLin() * 1000);
            j["G_dB"] = 10 * log10(r->getGLin());
            j["freq"] = r->getFreq();
            j["bandwidth"] = r->getBandwidth();
            j["sigma"] = r->getRCS();
            j["thresh_db"] = 10 * log10(r->getThreshLin());
            resp = makeJson(j);
        } else {
            bool ok = g_scene_mgr.removeRadar(sid, rid);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("not found");
        }
    } else if (req.method() == http::verb::put) {
        auto jr = body["radar"];
        if (jr.isNull()) { resp = jsonErr("need radar"); goto send; }
        int rid = jr["id"].asInt();
        ECMSim::Radar rad(
            rid, jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        bool ok = g_scene_mgr.updateRadar(sid, rid, rad);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("update failed");
    } else {
        resp = jsonErr("use POST/GET/PUT/DELETE");
    }
send:
    return resp;
}

/* /api/jammers — 干扰机批量操作
 * GET: 列出会话中所有干扰机
 * POST: 添加一个新干扰机 */
std::string handleJammers(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string resp;

    if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }

    if (req.method() == http::verb::get) {
        /* 遍历所有干扰机，转换为 JSON 数组 */
        Json::Value arr(Json::arrayValue);
        for (int jid : g_scene_mgr.getJammerIds(sid)) {
            auto* j = g_scene_mgr.getJammer(sid, jid);
            if (!j) continue;
            Json::Value jv;
            jv["id"] = j->getId();
            jv["x"] = j->getPos().first;
            jv["y"] = j->getPos().second;
            jv["Pj_dBm"] = 10 * log10(j->getPjLin() * 1000);
            jv["Gj_dB"] = 10 * log10(j->getGjLin());
            jv["jam_freq"] = j->getJamFreq();
            jv["jam_type"] = j->getJamType() == ECMSim::JamType::NOISE_JAM
                                  ? "NOISE_JAM" : "RANGE_DECEPT";
            arr.append(jv);
        }
        Json::Value ret;
        ret["success"] = true;
        ret["jammers"] = arr;
        resp = makeJson(ret);
    } else if (req.method() == http::verb::post) {
        /* 从 JSON 构造 Jammer 对象并添加到场景 */
        auto jv = body["jammer"];
        if (jv.isNull()) { resp = jsonErr("need jammer"); goto send; }
        ECMSim::JamType jt
            = jv["jam_type"].asString() == "NOISE_JAM"
                  ? ECMSim::JamType::NOISE_JAM : ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(
            jv["id"].asInt(), jv["x"].asDouble(), jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(),
            jv["jam_freq"].asDouble(), jt);
        bool ok = g_scene_mgr.addJammer(sid, jam);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("add failed");
    } else {
        resp = jsonErr("use GET/POST");
    }
send:
    return resp;
}

/* /api/jammer — 单个干扰机操作
 * GET: 获取指定干扰机参数
 * POST action=del: 删除干扰机
 * POST action=update: 更新干扰机参数
 * PUT: 更新干扰机参数 */
std::string handleJammer(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string action;
    std::string resp;

    if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int jid = body["jammer_id"].asInt();
            if (!jid) { resp = jsonErr("need jammer_id"); goto send; }

            bool ok = g_scene_mgr.removeJammer(sid, jid);
            if (ok) broadcastScene(sid);

            resp = ok ? jsonOk() : jsonErr("not found");
        } else if (action == "update") {
            auto jv = body["jammer"];
            if (jv.isNull()) { resp = jsonErr("need jammer"); goto send; }

            int jid = jv["id"].asInt();
            ECMSim::JamType jt
                = jv["jam_type"].asString() == "NOISE_JAM"
                      ? ECMSim::JamType::NOISE_JAM : ECMSim::JamType::RANGE_DECEPT;
            ECMSim::Jammer jam(
                jid, jv["x"].asDouble(), jv["y"].asDouble(),
                jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(),
                jv["jam_freq"].asDouble(), jt);

            bool ok = g_scene_mgr.updateJammer(sid, jid, jam);
            if (ok) broadcastScene(sid);

            resp = ok ? jsonOk() : jsonErr("update failed");
        } else {
            resp = jsonErr("unknown action");
        }
    } else if (req.method() == http::verb::get || req.method() == http::verb::delete_) {
        int jid = body["jammer_id"].asInt();
        if (!jid) { resp = jsonErr("need jammer_id"); goto send; }
        
        if (req.method() == http::verb::get) {
            auto* j = g_scene_mgr.getJammer(sid, jid);
            if (!j) { resp = jsonErr("not found"); goto send; }

            Json::Value jv;
            jv["success"] = true;
            jv["jammer_id"] = jid;
            jv["x"] = j->getPos().first;
            jv["y"] = j->getPos().second;
            jv["Pj_dBm"] = 10 * log10(j->getPjLin() * 1000);
            jv["Gj_dB"] = 10 * log10(j->getGjLin());
            jv["jam_freq"] = j->getJamFreq();
            jv["jam_type"] = j->getJamType() == ECMSim::JamType::NOISE_JAM
                                  ? "NOISE_JAM" : "RANGE_DECEPT";
            resp = makeJson(jv);
        } else {
            bool ok = g_scene_mgr.removeJammer(sid, jid);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("not found");
        }
    } else if (req.method() == http::verb::put) {
        auto jv = body["jammer"];
        if (jv.isNull()) { resp = jsonErr("need jammer"); goto send; }

        int jid = jv["id"].asInt();
        ECMSim::JamType jt
            = jv["jam_type"].asString() == "NOISE_JAM"
                  ? ECMSim::JamType::NOISE_JAM : ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(
            jid, jv["x"].asDouble(), jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(),
            jv["jam_freq"].asDouble(), jt);
            
        bool ok = g_scene_mgr.updateJammer(sid, jid, jam);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("update failed");
    } else {
        resp = jsonErr("use POST/GET/PUT/DELETE");
    }
send:
    return resp;
}

/* /api/simulate — 运行一次仿真
 * POST: 对会话中所有雷达+干扰机执行一次仿真，返回各雷达的 SINR、检测结果、
 *       干扰效果等。同时广播更新给前端。 */
std::string handleSimulate(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) {
        return jsonErr("use POST");
    }

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    if (sid.empty()) return jsonErr("need session_id");
    if (!g_scene_mgr.hasSession(sid)) return jsonErr("session not found");

    /* 执行仿真，获取每部雷达的结果 */
    auto results = g_scene_mgr.runSimulation(sid);
    Json::Value arr(Json::arrayValue);
    for (const auto& r : results) {
        Json::Value item;
        item["radar_id"] = r.radar_id;
        item["SINR_dB"] = r.sinr_db;
        item["signal_power_W"] = r.signal_power;
        item["total_effective_jam_power_W"] = r.total_effective_jam_power;
        item["jsr_dB"] = r.jsr_db;
        item["detect_success"] = r.detect_ok;
        item["jam_success_score"] = r.jam_success_score;
        item["is_deception_active"] = r.is_deception_active;
        item["decept_effect_score"] = r.decept_effect_score;

        /* 每部雷达对应的各干扰机明细 */
        Json::Value jd(Json::arrayValue);
        for (size_t i = 0; i < r.jammer_ids.size(); i++) {
            Json::Value j;
            j["jammer_id"] = r.jammer_ids[i];
            j["freq_delta_Hz"] = r.jam_freq_deltas[i];
            j["freq_match_factor"] = r.freq_match_factors[i];
            j["effective_power_W"] = r.effective_jam_powers[i];
            jd.append(j);
        }
        item["jammer_details"] = jd;
        arr.append(item);
    }
    Json::Value ret;
    ret["success"] = true;
    ret["sim_results"] = arr;
    std::string resp = makeJson(ret);
    broadcastScene(sid);  /* 仿真结果也推送给前端 */
    return resp;
}

/* /api/dqn/state — 获取 DQN 状态向量
 * GET: 返回指定干扰机的归一化状态向量，供 Python DQN 智能体使用。
 *       状态包含: 各雷达坐标/频率/带宽/距离/频差 + 干扰机功率/频率 */
std::string handleDqnState(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return jsonErr("use GET");

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    if (sid.empty() || !jid) return jsonErr("need session_id and jammer_id");

    auto state = g_scene_mgr.getStateForJammer(sid, jid);
    if (state.empty()) return jsonErr("jammer not found");

    Json::Value s(Json::arrayValue);
    for (double v : state) s.append(v);
    Json::Value ret;
    ret["success"] = true;
    ret["state"] = s;
    return makeJson(ret);
}

/* /api/dqn/action — 执行 DQN 动作
 * POST: 设置干扰机的功率和频率，供 Python DQN 智能体使用。
 *       执行后自动广播场景更新给前端。 */
std::string handleDqnAction(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) return jsonErr("use POST");

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    double pd = body["power_dbm"].asDouble();
    double fq = body["jam_freq"].asDouble();
    if (sid.empty() || !jid) return jsonErr("need session_id and jammer_id");

    bool ok = g_scene_mgr.executeJammerAction(sid, jid, pd, fq);
    if (ok) broadcastScene(sid);  /* DQN 动作修改后推送给前端 */
    return ok ? jsonOk() : jsonErr("jammer not found");
}

/* ====== 路由分发 ======
 * 根据请求路径 (target) 分发到对应的 handler。
 * 自动截断查询字符串 (? 后的部分)。 */
std::string routeRequest(const http::request<http::string_body>& req) {
    std::string target = svToString(req.target());
    size_t qpos = target.find('?');
    if (qpos != std::string::npos) {
        target = target.substr(0, qpos);
    }

    if (target == "/") {
        return g_index_html;
    } else if (target == "/api/scene") {
        return handleScene(req);
    } else if (target == "/api/radars") {
        return handleRadars(req);
    } else if (target == "/api/radar") {
        return handleRadar(req);
    } else if (target == "/api/jammers") {
        return handleJammers(req);
    } else if (target == "/api/jammer") {
        return handleJammer(req);
    } else if (target == "/api/simulate") {
        return handleSimulate(req);
    } else if (target == "/api/dqn/state") {
        return handleDqnState(req);
    } else if (target == "/api/dqn/action") {
        return handleDqnAction(req);
    } else {
        return jsonErr("unknown route: " + target);
    }
}

/* ====== HTTP 会话 ======
 * 管理单个 HTTP 连接的生命周期（异步读取请求 → 处理 → 异步写入响应）。
 * 如果检测到 WebSocket 升级请求，转交给 websocket_session 处理。 */
class http_session : public std::enable_shared_from_this<http_session> {
public:
    http_session(tcp::socket socket)
        : socket_(std::move(socket)) {}

    void start() { do_read(); }

private:
    beast::flat_buffer buffer_;
    tcp::socket socket_;
    http::request<http::string_body> req_;

    /* 检测是否为 WebSocket 升级请求 */
    static bool isWebSocketUpgrade(const http::request<http::string_body>& req) {
        auto const& h = req.base();
        auto it = h.find(http::field::connection);
        if (it == h.end()) return false;
        if (it->value().find("Upgrade") == std::string::npos) return false;
        it = h.find(http::field::upgrade);
        if (it == h.end()) return false;
        return it->value() == "websocket";
    }

    /* 从 URL 查询字符串中提取 session_id 参数 */
    static std::string extractSessionId(const std::string& target) {
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

    /* 异步读取 HTTP 请求 */
    void do_read() {
        http::async_read(socket_, buffer_, req_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) return;
                self->process();
            });
    }

    /* 处理请求：WebSocket 升级或普通 HTTP 响应 */
    void process() {
        std::string target = svToString(req_.target());

        /* WebSocket 升级: 将 socket 和请求转交给 websocket_session */
        if (isWebSocketUpgrade(req_)) {
            std::string sid = extractSessionId(target);
            if (!sid.empty()) {
                auto ws = std::make_shared<websocket_session>(
                    std::move(socket_), std::move(req_), sid);
                ws->start();
            }
            return;
        }

        /* 普通 HTTP: 路由分发 → 构造响应 → 异步写入 */
        std::string response_str = routeRequest(req_);
        bool is_html = (target == "/");

        http::response<http::string_body> res{http::status::ok, req_.version()};
        res.set(http::field::server, "ECMSim-beast");
        res.set(http::field::content_type,
                is_html ? "text/html" : "application/json");
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
};

/* ====== HTTP 服务器 ======
 * 异步接受 TCP 连接，为每个连接创建 http_session。 */
class http_server {
public:
    http_server(net::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc) {
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) { std::cerr << "[ERR] open: " << ec.message() << "\n"; return; }
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) { std::cerr << "[ERR] set_option: " << ec.message() << "\n"; return; }
        acceptor_.bind(endpoint, ec);
        if (ec) { std::cerr << "[ERR] bind: " << ec.message() << "\n"; return; }
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) { std::cerr << "[ERR] listen: " << ec.message() << "\n"; return; }
        do_accept();
    }

private:
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (ec) { do_accept(); return; }
                std::make_shared<http_session>(std::move(socket))->start();
                do_accept();
            });
    }
};

/* ====== gRPC 服务 ======
 * 实现 AgentService 的三个 RPC 方法，供 Python DQN 智能体调用。
 * ExecuteAction 和 StepSimulation 执行后自动广播场景更新给前端。 */
class AgentSvc final : public ecmsim::AgentService::Service {

    /* 获取干扰机状态向量（归一化的雷达坐标/频率/距离/频差 + 干扰机功率/频率） */
    grpc::Status GetState(
        grpc::ServerContext*,
        const ecmsim::StateRequest* rq,
        ecmsim::StateResponse* rp) override {
        auto s = g_scene_mgr.getStateForJammer(rq->session_id(), rq->jammer_id());
        if (s.empty()) {
            rp->set_success(false);
            rp->set_error("not found");
            return grpc::Status::OK;
        }
        for (double v : s) rp->add_state(v);
        rp->set_success(true);
        return grpc::Status::OK;
    }

    /* 执行干扰机动作（设置功率和频率），执行后广播更新 */
    grpc::Status ExecuteAction(
        grpc::ServerContext*,
        const ecmsim::ActionRequest* rq,
        ecmsim::ActionResponse* rp) override {
        bool ok = g_scene_mgr.executeJammerAction(
            rq->session_id(), rq->jammer_id(), rq->power_dbm(), rq->jam_freq());
        rp->set_success(ok);
        if (!ok) {
            rp->set_error("not found");
        } else {
            broadcastScene(rq->session_id());  /* gRPC 修改 → WebSocket 推送 */
        }
        return grpc::Status::OK;
    }

    /* 执行一次仿真，返回所有雷达的 SINR/检测/干扰效果，执行后广播更新 */
    grpc::Status StepSimulation(
        grpc::ServerContext*,
        const ecmsim::StepRequest* rq,
        ecmsim::StepResponse* rp) override {
        auto rs = g_scene_mgr.runSimulation(rq->session_id());
        if (rs.empty()) {
            rp->set_success(false);
            rp->set_error("no radars");
            return grpc::Status::OK;
        }
        for (const auto& r : rs) {
            auto* p = rp->add_results();
            p->set_radar_id(r.radar_id);
            p->set_sinr_db(r.sinr_db);
            p->set_detect_success(r.detect_ok);
            p->set_jam_success_score(r.jam_success_score);
            p->set_total_effective_jam_power(r.total_effective_jam_power);
            p->set_jsr_db(r.jsr_db);
            p->set_is_deception_active(r.is_deception_active);
            p->set_decept_effect_score(r.decept_effect_score);
            for (double d : r.jam_freq_deltas) p->add_jam_freq_deltas(d);
            for (double z : r.freq_match_factors) p->add_freq_match_factors(z);
        }
        rp->set_success(true);
        broadcastScene(rq->session_id());  /* 仿真完成 → WebSocket 推送 */
        return grpc::Status::OK;
    }
};

/* ====== 主函数 ======
 * 启动流程:
 *   1. 加载前端 HTML 文件
 *   2. 创建 io_context 和 HTTP 服务器（监听 :8080）
 *   3. 创建 gRPC 服务器（监听 :50051）
 *   4. 启动多线程事件循环
 *   5. 等待退出 */
int main(int argc, char* argv[]) {
    /* 加载前端页面（可通过命令行参数指定静态文件目录，默认 "static"） */
    const char* staticDir = argc >= 2 ? argv[1] : "static";
    std::string indexPath = std::string(staticDir) + "/index_v2.html";
    if (!loadFile(indexPath, g_index_html)) {
        std::cerr << "[ERR] Cannot load " << indexPath << "\n";
        return 1;
    }
    std::cout << "[INFO] Loaded " << indexPath << " ("
              << g_index_html.size() << " bytes)\n";

    /* 创建 boost::asio io_context，支持多线程并发处理 */
    constexpr int num_threads = 4;
    net::io_context ioc{num_threads};

    /* 启动 HTTP 服务器（:8080） */
    tcp::endpoint endpoint{net::ip::make_address("0.0.0.0"), 8080};
    http_server server(ioc, endpoint);
    std::cout << "[INFO] HTTP :8080\n";

    /* 启动 gRPC 服务器（:50051） */
    AgentSvc agentSvc;
    grpc::ServerBuilder gb;
    gb.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    gb.RegisterService(&agentSvc);
    auto gs = gb.BuildAndStart();
    if (!gs) {
        std::cerr << "[ERR] gRPC failed\n";
        return 1;
    }
    std::cout << "[INFO] gRPC :50051\n";

    /* 启动 (num_threads-1) 个工作线程 + 主线程运行 io_context */
    std::vector<std::thread> threads;
    threads.reserve(num_threads - 1);
    for (int i = 0; i < num_threads - 1; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }
    gs->Wait();   /* 阻塞等待 gRPC 服务器退出 */
    ioc.run();    /* 主线程也运行 io_context */
    for (auto& t : threads) t.join();

    return 0;
}
