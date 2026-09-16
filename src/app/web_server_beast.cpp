/***
 * ECMSim HTTP+gRPC+WebSocket Server — boost.beast implementation
 * Routes:
 *   POST   /api/scene           → create session
 *   GET    /api/scene           → get scene (body: {session_id})
 *   DELETE /api/scene           → delete session
 *   GET    /api/radars          → list radars
 *   POST   /api/radars          → add radar
 *   GET    /api/radar           → get radar
 *   PUT    /api/radar           → update radar
 *   DELETE /api/radar           → delete radar
 *   GET    /api/jammers         → list jammers
 *   POST   /api/jammers         → add jammer
 *   GET    /api/jammer          → get jammer
 *   PUT    /api/jammer          → update jammer
 *   DELETE /api/jammer          → delete jammer
 *   POST   /api/simulate        → run simulation
 *   GET    /api/dqn/state       → DQN state
 *   POST   /api/dqn/action      → DQN action
 *   WS     /ws?session_id=xxx   → WebSocket sync
 * HTTP :8080, gRPC :50051
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

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

static ECMSim::SceneManager g_scene_mgr;
static std::string g_index_html;

/* ====== Forward Declarations ====== */
class websocket_session;
class ws_broadcaster;
static std::string svToString(beast::string_view sv);
std::string routeRequest(const http::request<http::string_body>& req);

/* ====== WebSocket Broadcaster (forward-declared, methods defined later) ====== */
class ws_broadcaster {
public:
    void subscribe(const std::string& sid, websocket_session* ws);
    void unsubscribe(const std::string& sid, websocket_session* ws);
    void broadcast(const std::string& sid, const std::string& msg);

private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::set<websocket_session*>> sessions_;
};

/* Global broadcaster instance (before websocket_session uses it) */
ws_broadcaster g_broadcaster;

/* ====== WebSocket Session (defined after ws_broadcaster) ====== */
class websocket_session : public std::enable_shared_from_this<websocket_session> {
public:
    websocket_session(tcp::socket socket, const std::string& sid): ws_(std::move(socket)), sid_(sid) {}

    void start() {
        ws_.async_accept(
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) return;
                g_broadcaster.subscribe(self->sid_, self.get());
                self->do_read();
            });
    }

    void send(const std::string& msg) {
        auto buf = std::make_shared<std::string>(msg);
        ws_.async_write(net::buffer(*buf),
            [self = shared_from_this(), buf](beast::error_code ec, std::size_t) {
                if (ec) self->on_close();
            });
    }

private:
    void do_read() {
        ws_.async_read(buffer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) { self->on_close(); return; }
                self->buffer_.consume(self->buffer_.size());
                self->do_read();
            });
    }

    void on_close() {
        g_broadcaster.unsubscribe(sid_, this);
    }

    websocket::stream<tcp::socket> ws_;
    std::string sid_;
    beast::flat_buffer buffer_;
};

/* Define ws_broadcaster methods after websocket_session is complete */
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

/* ====== Helper Functions ====== */
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

static std::string makeJson(const Json::Value& v) {
    Json::StyledWriter w;
    return w.write(v);
}

static std::string jsonOk() {
    return "{\"success\":true}\n";
}

static std::string jsonErr(const std::string& msg) {
    Json::Value r;
    r["success"] = false;
    r["error"] = msg;
    return makeJson(r);
}

static Json::Value parseJson(const std::string& str) {
    Json::Value r;
    Json::Reader rd;
    rd.parse(str, r, false);
    return r;
}

static std::string svToString(beast::string_view sv) {
    return std::string(sv.data(), sv.size());
}

static std::string sceneToJson(const std::string& sid) {
    Json::Value v;
    Json::Reader rd;
    rd.parse(g_scene_mgr.getSceneJson(sid), v);
    v["success"] = true;
    v["session_id"] = sid;
    return makeJson(v);
}

static void broadcastScene(const std::string& sid) {
    g_broadcaster.broadcast(sid, sceneToJson(sid));
}

/* ====== HTTP Request Handlers ====== */
std::string handleScene(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid;
    std::string action;
    std::string resp;

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action.empty()) {
            sid = g_scene_mgr.createSession();
            Json::Value r;
            r["success"] = true;
            r["session_id"] = sid;
            resp = makeJson(r);
        } else if (action == "config") {
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
        Json::Value arr(Json::arrayValue);
        for (int id : g_scene_mgr.getRadarIds(sid)) {
            auto* r = g_scene_mgr.getRadar(sid, id);
            if (!r) {
                continue;
            }
            Json::Value j;
            j["id"] = r->getId();
            j["x"] = r->getPos().first;
            j["y"] = r->getPos().second;
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
        auto jr = body["radar"];
        if (jr.isNull()) {
            resp = jsonErr("need radar object");
            goto send;
        }
        ECMSim::Radar rad(
            jr["id"].asInt(),
            jr["x"].asDouble(),
            jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(),
            jr["G_dB"].asDouble(),
            jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(),
            jr["sigma"].asDouble(),
            jr["thresh_db"].asDouble());
        bool ok = g_scene_mgr.addRadar(sid, rad);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("add failed");
    } else {
        resp = jsonErr("use GET/POST");
    }
send:
    return resp;
}

std::string handleRadar(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string action;
    std::string resp;

    if (sid.empty()) {
        resp = jsonErr("need session_id");
        goto send;
    }
    if (!g_scene_mgr.hasSession(sid)) {
        resp = jsonErr("session not found");
        goto send;
    }

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int rid = body["radar_id"].asInt();
            if (!rid) {
                resp = jsonErr("need radar_id");
                goto send;
            }
            bool ok = g_scene_mgr.removeRadar(sid, rid);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("not found");
        } else if (action == "update") {
            auto jr = body["radar"];
            if (jr.isNull()) {
                resp = jsonErr("need radar");
                goto send;
            }
            int rid = jr["id"].asInt();
            ECMSim::Radar rad(
                rid,
                jr["x"].asDouble(),
                jr["y"].asDouble(),
                jr["Pt_dBm"].asDouble(),
                jr["G_dB"].asDouble(),
                jr["freq"].asDouble(),
                jr["bandwidth"].asDouble(),
                jr["sigma"].asDouble(),
                jr["thresh_db"].asDouble());
            bool ok = g_scene_mgr.updateRadar(sid, rid, rad);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("update failed");
        } else {
            resp = jsonErr("unknown action");
        }
    } else if (req.method() == http::verb::get
               || req.method() == http::verb::delete_) {
        int rid = body["radar_id"].asInt();
        if (!rid) {
            resp = jsonErr("need radar_id");
            goto send;
        }
        if (req.method() == http::verb::get) {
            auto* r = g_scene_mgr.getRadar(sid, rid);
            if (!r) {
                resp = jsonErr("not found");
                goto send;
            }
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
        if (jr.isNull()) {
            resp = jsonErr("need radar");
            goto send;
        }
        int rid = jr["id"].asInt();
        ECMSim::Radar rad(
            rid,
            jr["x"].asDouble(),
            jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(),
            jr["G_dB"].asDouble(),
            jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(),
            jr["sigma"].asDouble(),
            jr["thresh_db"].asDouble());
        bool ok = g_scene_mgr.updateRadar(sid, rid, rad);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("update failed");
    } else {
        resp = jsonErr("use POST/GET/PUT/DELETE");
    }
send:
    return resp;
}

std::string handleJammers(const http::request<http::string_body>& req) {
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
        Json::Value arr(Json::arrayValue);
        for (int jid : g_scene_mgr.getJammerIds(sid)) {
            auto* j = g_scene_mgr.getJammer(sid, jid);
            if (!j) {
                continue;
            }
            Json::Value jv;
            jv["id"] = j->getId();
            jv["x"] = j->getPos().first;
            jv["y"] = j->getPos().second;
            jv["Pj_dBm"] = 10 * log10(j->getPjLin() * 1000);
            jv["Gj_dB"] = 10 * log10(j->getGjLin());
            jv["jam_freq"] = j->getJamFreq();
            jv["jam_type"]
                = j->getJamType() == ECMSim::JamType::NOISE_JAM
                      ? "NOISE_JAM"
                      : "RANGE_DECEPT";
            arr.append(jv);
        }
        Json::Value ret;
        ret["success"] = true;
        ret["jammers"] = arr;
        resp = makeJson(ret);
    } else if (req.method() == http::verb::post) {
        auto jv = body["jammer"];
        if (jv.isNull()) {
            resp = jsonErr("need jammer");
            goto send;
        }
        ECMSim::JamType jt
            = jv["jam_type"].asString() == "NOISE_JAM"
                  ? ECMSim::JamType::NOISE_JAM
                  : ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(
            jv["id"].asInt(),
            jv["x"].asDouble(),
            jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(),
            jv["Gj_dB"].asDouble(),
            jv["jam_freq"].asDouble(),
            jt);
        bool ok = g_scene_mgr.addJammer(sid, jam);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("add failed");
    } else {
        resp = jsonErr("use GET/POST");
    }
send:
    return resp;
}

std::string handleJammer(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string action;
    std::string resp;

    if (sid.empty()) {
        resp = jsonErr("need session_id");
        goto send;
    }
    if (!g_scene_mgr.hasSession(sid)) {
        resp = jsonErr("session not found");
        goto send;
    }

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int jid = body["jammer_id"].asInt();
            if (!jid) {
                resp = jsonErr("need jammer_id");
                goto send;
            }
            bool ok = g_scene_mgr.removeJammer(sid, jid);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("not found");
        } else if (action == "update") {
            auto jv = body["jammer"];
            if (jv.isNull()) {
                resp = jsonErr("need jammer");
                goto send;
            }
            int jid = jv["id"].asInt();
            ECMSim::JamType jt
                = jv["jam_type"].asString() == "NOISE_JAM"
                      ? ECMSim::JamType::NOISE_JAM
                      : ECMSim::JamType::RANGE_DECEPT;
            ECMSim::Jammer jam(
                jid,
                jv["x"].asDouble(),
                jv["y"].asDouble(),
                jv["Pj_dBm"].asDouble(),
                jv["Gj_dB"].asDouble(),
                jv["jam_freq"].asDouble(),
                jt);
            bool ok = g_scene_mgr.updateJammer(sid, jid, jam);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("update failed");
        } else {
            resp = jsonErr("unknown action");
        }
    } else if (req.method() == http::verb::get
               || req.method() == http::verb::delete_) {
        int jid = body["jammer_id"].asInt();
        if (!jid) {
            resp = jsonErr("need jammer_id");
            goto send;
        }
        if (req.method() == http::verb::get) {
            auto* j = g_scene_mgr.getJammer(sid, jid);
            if (!j) {
                resp = jsonErr("not found");
                goto send;
            }
            Json::Value jv;
            jv["success"] = true;
            jv["jammer_id"] = jid;
            jv["x"] = j->getPos().first;
            jv["y"] = j->getPos().second;
            jv["Pj_dBm"] = 10 * log10(j->getPjLin() * 1000);
            jv["Gj_dB"] = 10 * log10(j->getGjLin());
            jv["jam_freq"] = j->getJamFreq();
            jv["jam_type"]
                = j->getJamType() == ECMSim::JamType::NOISE_JAM
                      ? "NOISE_JAM"
                      : "RANGE_DECEPT";
            resp = makeJson(jv);
        } else {
            bool ok = g_scene_mgr.removeJammer(sid, jid);
            if (ok) broadcastScene(sid);
            resp = ok ? jsonOk() : jsonErr("not found");
        }
    } else if (req.method() == http::verb::put) {
        auto jv = body["jammer"];
        if (jv.isNull()) {
            resp = jsonErr("need jammer");
            goto send;
        }
        int jid = jv["id"].asInt();
        ECMSim::JamType jt
            = jv["jam_type"].asString() == "NOISE_JAM"
                  ? ECMSim::JamType::NOISE_JAM
                  : ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(
            jid,
            jv["x"].asDouble(),
            jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(),
            jv["Gj_dB"].asDouble(),
            jv["jam_freq"].asDouble(),
            jt);
        bool ok = g_scene_mgr.updateJammer(sid, jid, jam);
        if (ok) broadcastScene(sid);
        resp = ok ? jsonOk() : jsonErr("update failed");
    } else {
        resp = jsonErr("use POST/GET/PUT/DELETE");
    }
send:
    return resp;
}

std::string handleSimulate(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) {
        return jsonErr("use POST");
    }

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    if (sid.empty()) {
        return jsonErr("need session_id");
    }
    if (!g_scene_mgr.hasSession(sid)) {
        return jsonErr("session not found");
    }

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
    broadcastScene(sid);
    return resp;
}

std::string handleDqnState(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) {
        return jsonErr("use GET");
    }

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    if (sid.empty() || !jid) {
        return jsonErr("need session_id and jammer_id");
    }

    auto state = g_scene_mgr.getStateForJammer(sid, jid);
    if (state.empty()) {
        return jsonErr("jammer not found");
    }

    Json::Value s(Json::arrayValue);
    for (double v : state) {
        s.append(v);
    }
    Json::Value ret;
    ret["success"] = true;
    ret["state"] = s;
    return makeJson(ret);
}

std::string handleDqnAction(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) {
        return jsonErr("use POST");
    }

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    double pd = body["power_dbm"].asDouble();
    double fq = body["jam_freq"].asDouble();
    if (sid.empty() || !jid) {
        return jsonErr("need session_id and jammer_id");
    }

    bool ok = g_scene_mgr.executeJammerAction(sid, jid, pd, fq);
    if (ok) broadcastScene(sid);
    return ok ? jsonOk() : jsonErr("jammer not found");
}

/* Route dispatch based on target path */
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

/* ====== HTTP Session ====== */
class http_session : public std::enable_shared_from_this<http_session> {
public:
    http_session(tcp::socket socket)
        : socket_(std::move(socket)) {}

    void start() {
        do_read();
    }

private:
    beast::flat_buffer buffer_;
    tcp::socket socket_;
    http::request<http::string_body> req_;

    static bool isWebSocketUpgrade(const http::request<http::string_body>& req) {
        auto const& h = req.base();
        auto it = h.find(http::field::connection);
        if (it == h.end()) return false;
        if (it->value().find("Upgrade") == std::string::npos) return false;
        it = h.find(http::field::upgrade);
        if (it == h.end()) return false;
        return it->value() == "websocket";
    }

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

    void do_read() {
        http::async_read(socket_, buffer_, req_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) return;
                self->process();
            });
    }

    void process() {
        std::string target = svToString(req_.target());

        if (isWebSocketUpgrade(req_)) {
            std::string sid = extractSessionId(target);
            if (!sid.empty()) {
                auto ws = std::make_shared<websocket_session>(
                    std::move(socket_), sid);
                ws->start();
            }
            return;
        }

        std::string response_str = routeRequest(req_);
        bool is_html = (target == "/");

        http::response<http::string_body> res{http::status::ok,
                                              req_.version()};
        res.set(http::field::server, "ECMSim-beast");
        res.set(http::field::content_type,
                is_html ? "text/html" : "application/json");
        res.keep_alive(req_.keep_alive());
        res.body() = response_str;
        res.prepare_payload();

        auto res_shared
            = std::make_shared<http::response<http::string_body>>(
                std::move(res));
        http::async_write(
            socket_, *res_shared,
            [self = shared_from_this(), res_shared](beast::error_code ec,
                                                   std::size_t) {
                if (ec) return;
                self->socket_.shutdown(
                    tcp::socket::shutdown_send, ec);
            });
    }
};

/* ====== HTTP Server ====== */
class http_server {
public:
    http_server(net::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc) {
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            std::cerr << "[ERR] open: " << ec.message() << "\n";
            return;
        }
        acceptor_.set_option(
            net::socket_base::reuse_address(true), ec);
        if (ec) {
            std::cerr << "[ERR] set_option: " << ec.message() << "\n";
            return;
        }
        acceptor_.bind(endpoint, ec);
        if (ec) {
            std::cerr << "[ERR] bind: " << ec.message() << "\n";
            return;
        }
        acceptor_.listen(
            net::socket_base::max_listen_connections, ec);
        if (ec) {
            std::cerr << "[ERR] listen: " << ec.message() << "\n";
            return;
        }
        do_accept();
    }

private:
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (ec) {
                    do_accept();
                    return;
                }
                std::make_shared<http_session>(std::move(socket))
                    ->start();
                do_accept();
            });
    }
};

/* ====== gRPC Service ====== */
class AgentSvc final : public ecmsim::AgentService::Service {
    grpc::Status GetState(
        grpc::ServerContext*,
        const ecmsim::StateRequest* rq,
        ecmsim::StateResponse* rp) override {
        auto s = g_scene_mgr.getStateForJammer(
            rq->session_id(), rq->jammer_id());
        if (s.empty()) {
            rp->set_success(false);
            rp->set_error("not found");
            return grpc::Status::OK;
        }
        for (double v : s) {
            rp->add_state(v);
        }
        rp->set_success(true);
        return grpc::Status::OK;
    }
    grpc::Status ExecuteAction(
        grpc::ServerContext*,
        const ecmsim::ActionRequest* rq,
        ecmsim::ActionResponse* rp) override {
        bool ok = g_scene_mgr.executeJammerAction(
            rq->session_id(),
            rq->jammer_id(),
            rq->power_dbm(),
            rq->jam_freq());
        rp->set_success(ok);
        if (!ok) {
            rp->set_error("not found");
        } else {
            broadcastScene(rq->session_id());
        }
        return grpc::Status::OK;
    }
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
            p->set_total_effective_jam_power(
                r.total_effective_jam_power);
            p->set_jsr_db(r.jsr_db);
            p->set_is_deception_active(r.is_deception_active);
            p->set_decept_effect_score(r.decept_effect_score);
            for (double d : r.jam_freq_deltas) {
                p->add_jam_freq_deltas(d);
            }
            for (double z : r.freq_match_factors) {
                p->add_freq_match_factors(z);
            }
        }
        rp->set_success(true);
        broadcastScene(rq->session_id());
        return grpc::Status::OK;
    }
};

/* ====== Main ====== */
int main(int argc, char* argv[]) {
    const char* staticDir = argc >= 2 ? argv[1] : "static";
    std::string indexPath = std::string(staticDir) + "/index_v2.html";
    if (!loadFile(indexPath, g_index_html)) {
        std::cerr << "[ERR] Cannot load " << indexPath << "\n";
        return 1;
    }
    std::cout << "[INFO] Loaded " << indexPath << " ("
              << g_index_html.size() << " bytes)\n";

    constexpr int num_threads = 4;
    net::io_context ioc{num_threads};

    tcp::endpoint endpoint{
        net::ip::make_address("0.0.0.0"), 8080};
    http_server server(ioc, endpoint);
    std::cout << "[INFO] HTTP :8080\n";

    AgentSvc agentSvc;
    grpc::ServerBuilder gb;
    gb.AddListeningPort(
        "0.0.0.0:50051", grpc::InsecureServerCredentials());
    gb.RegisterService(&agentSvc);

    auto gs = gb.BuildAndStart();
    if (!gs) {
        std::cerr << "[ERR] gRPC failed\n";
        return 1;
    }
    std::cout << "[INFO] gRPC :50051\n";

    std::vector<std::thread> threads;
    threads.reserve(num_threads - 1);
    for (int i = 0; i < num_threads - 1; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }
    gs->Wait();
    ioc.run();
    for (auto& t : threads) {
        t.join();
    }

    return 0;
}
