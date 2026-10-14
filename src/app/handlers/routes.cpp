#include <app/handlers/routes.h>
#include <app/framework/common.h>
#include <app/framework/ws_session.h>
#include <app/framework/router.h>

#include <app/framework/router.h>
#include <app/framework/static_resource.h>

#include <sence/scene_manager.h>
#include <jsoncpp/json/json.h>

static RouteResponse makeResp(const std::string& body) {
    return RouteResponse{body, "application/json", http::status::ok};
}

static RouteResponse makeErr(const std::string& msg) {
    Json::Value r;
    r["success"] = false;
    r["error"] = msg;
    Json::StyledWriter w;
    return RouteResponse{w.write(r), "application/json", http::status::ok};
}

RouteResponse handleScene(const http::request<http::string_body>& req) {
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
            return makeResp(makeJson(r));
        } else if (action == "config") {
            sid = body["session_id"].asString();
            if (sid.empty()) { return makeErr("need session_id"); }
            if (!g_scene_mgr.hasSession(sid)) { return makeErr("session not found"); }
            Json::Value v;
            Json::Reader rd;
            rd.parse(g_scene_mgr.getSceneJson(sid), v);
            v["success"] = true;
            v["session_id"] = sid;
            return makeResp(makeJson(v));
        } else if (action == "delete") {
            sid = body["session_id"].asString();
            if (sid.empty()) { return makeErr("need session_id"); }
            return g_scene_mgr.deleteSession(sid) ? makeResp(jsonOk()) : makeErr("session not found");
        } else {
            return makeErr("unknown action");
        }
    } else if (req.method() == http::verb::get) {
        sid = body["session_id"].asString();
        if (sid.empty()) { return makeErr("need session_id"); }
        if (!g_scene_mgr.hasSession(sid)) { return makeErr("session not found"); }
        Json::Value v;
        Json::Reader rd;
        rd.parse(g_scene_mgr.getSceneJson(sid), v);
        v["success"] = true;
        v["session_id"] = sid;
        return makeResp(makeJson(v));
    } else if (req.method() == http::verb::delete_) {
        sid = body["session_id"].asString();
        if (sid.empty()) { return makeErr("need session_id"); }
        return g_scene_mgr.deleteSession(sid) ? makeResp(jsonOk()) : makeErr("session not found");
    } else {
        return makeErr("use POST/GET/DELETE");
    }
}

RouteResponse handleRadars(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string resp;

    if (sid.empty()) { return makeErr("need session_id"); }
    if (!g_scene_mgr.hasSession(sid)) { return makeErr("session not found"); }

    if (req.method() == http::verb::get) {
        Json::Value arr(Json::arrayValue);
        for (int id : g_scene_mgr.getRadarIds(sid)) {
            auto* r = g_scene_mgr.getRadar(sid, id);
            if (!r) continue;
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
        return makeResp(makeJson(ret));
    } else if (req.method() == http::verb::post) {
        auto jr = body["radar"];
        if (jr.isNull()) { return makeErr("need radar object"); }
        ECMSim::Radar rad(
            jr["id"].asInt(), jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        bool ok = g_scene_mgr.addRadar(sid, rad);
        if (ok) broadcastScene(sid);
        return ok ? makeResp(jsonOk()) : makeErr("add failed");
    } else {
        return makeErr("use GET/POST");
    }
}

RouteResponse handleRadar(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string action;
    std::string resp;

    if (sid.empty()) { return makeErr("need session_id"); }
    if (!g_scene_mgr.hasSession(sid)) { return makeErr("session not found"); }

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int rid = body["radar_id"].asInt();
            if (!rid) { return makeErr("need radar_id"); }
            bool ok = g_scene_mgr.removeRadar(sid, rid);
            if (ok) broadcastScene(sid);
            return ok ? makeResp(jsonOk()) : makeErr("not found");
        } else if (action == "update") {
            auto jr = body["radar"];
            if (jr.isNull()) { return makeErr("need radar"); }
            int rid = jr["id"].asInt();
            ECMSim::Radar rad(
                rid, jr["x"].asDouble(), jr["y"].asDouble(),
                jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
                jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
            bool ok = g_scene_mgr.updateRadar(sid, rid, rad);
            if (ok) broadcastScene(sid);
            return ok ? makeResp(jsonOk()) : makeErr("update failed");
        } else {
            return makeErr("unknown action");
        }
    } else if (req.method() == http::verb::get || req.method() == http::verb::delete_) {
        int rid = body["radar_id"].asInt();
        if (!rid) { return makeErr("need radar_id"); }
        if (req.method() == http::verb::get) {
            auto* r = g_scene_mgr.getRadar(sid, rid);
            if (!r) { return makeErr("not found"); }
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
            return makeResp(makeJson(j));
        } else {
            bool ok = g_scene_mgr.removeRadar(sid, rid);
            if (ok) broadcastScene(sid);
            return ok ? makeResp(jsonOk()) : makeErr("not found");
        }
    } else if (req.method() == http::verb::put) {
        auto jr = body["radar"];
        if (jr.isNull()) { return makeErr("need radar"); }
        int rid = jr["id"].asInt();
        ECMSim::Radar rad(
            rid, jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        bool ok = g_scene_mgr.updateRadar(sid, rid, rad);
        if (ok) broadcastScene(sid);
        return ok ? makeResp(jsonOk()) : makeErr("update failed");
    } else {
        return makeErr("use POST/GET/PUT/DELETE");
    }
}

RouteResponse handleJammers(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string resp;

    if (sid.empty()) { return makeErr("need session_id"); }
    if (!g_scene_mgr.hasSession(sid)) { return makeErr("session not found"); }

    if (req.method() == http::verb::get) {
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
        return makeResp(makeJson(ret));
    } else if (req.method() == http::verb::post) {
        auto jv = body["jammer"];
        if (jv.isNull()) { return makeErr("need jammer"); }
        ECMSim::JamType jt
            = jv["jam_type"].asString() == "NOISE_JAM"
                  ? ECMSim::JamType::NOISE_JAM : ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(
            jv["id"].asInt(), jv["x"].asDouble(), jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(),
            jv["jam_freq"].asDouble(), jt);
        bool ok = g_scene_mgr.addJammer(sid, jam);
        if (ok) broadcastScene(sid);
        return ok ? makeResp(jsonOk()) : makeErr("add failed");
    } else {
        return makeErr("use GET/POST");
    }
}

RouteResponse handleJammer(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string action;
    std::string resp;

    if (sid.empty()) { return makeErr("need session_id"); }
    if (!g_scene_mgr.hasSession(sid)) { return makeErr("session not found"); }

    if (req.method() == http::verb::post) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int jid = body["jammer_id"].asInt();
            if (!jid) { return makeErr("need jammer_id"); }
            bool ok = g_scene_mgr.removeJammer(sid, jid);
            if (ok) broadcastScene(sid);
            return ok ? makeResp(jsonOk()) : makeErr("not found");
        } else if (action == "update") {
            auto jv = body["jammer"];
            if (jv.isNull()) { return makeErr("need jammer"); }
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
            return ok ? makeResp(jsonOk()) : makeErr("update failed");
        } else {
            return makeErr("unknown action");
        }
    } else if (req.method() == http::verb::get || req.method() == http::verb::delete_) {
        int jid = body["jammer_id"].asInt();
        if (!jid) { return makeErr("need jammer_id"); }
        if (req.method() == http::verb::get) {
            auto* j = g_scene_mgr.getJammer(sid, jid);
            if (!j) { return makeErr("not found"); }
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
            return makeResp(makeJson(jv));
        } else {
            bool ok = g_scene_mgr.removeJammer(sid, jid);
            if (ok) broadcastScene(sid);
            return ok ? makeResp(jsonOk()) : makeErr("not found");
        }
    } else if (req.method() == http::verb::put) {
        auto jv = body["jammer"];
        if (jv.isNull()) { return makeErr("need jammer"); }
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
        return ok ? makeResp(jsonOk()) : makeErr("update failed");
    } else {
        return makeErr("use POST/GET/PUT/DELETE");
    }
}

RouteResponse handleSimulate(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) {
        return makeErr("use POST");
    }

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    if (sid.empty()) return makeErr("need session_id");
    if (!g_scene_mgr.hasSession(sid)) return makeErr("session not found");

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
    // broadcastScene(sid);  /* 仿真不改变实体参数，无需推送（雷达/干扰机更新已由各自 handler 广播） */
    return makeResp(makeJson(ret));
}

RouteResponse handleDqnState(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return makeErr("use GET");

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    if (sid.empty() || !jid) return makeErr("need session_id and jammer_id");

    auto state = g_scene_mgr.getStateForJammer(sid, jid);
    if (state.empty()) return makeErr("jammer not found");

    Json::Value s(Json::arrayValue);
    for (double v : state) s.append(v);
    Json::Value ret;
    ret["success"] = true;
    ret["state"] = s;
    return makeResp(makeJson(ret));
}

RouteResponse handleDqnAction(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) return makeErr("use POST");

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    double pd = body["power_dbm"].asDouble();
    double fq = body["jam_freq"].asDouble();
    if (sid.empty() || !jid) return makeErr("need session_id and jammer_id");

    bool ok = g_scene_mgr.executeJammerAction(sid, jid, pd, fq);
    if (ok) broadcastScene(sid);
    return ok ? makeResp(jsonOk()) : makeErr("jammer not found");
}

RouteResponse handleSceneList(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return makeErr("use GET");

    auto sessions = g_scene_mgr.getAllSessions();
    Json::Value arr(Json::arrayValue);

    for (const auto& [sid, last_access] : sessions) {
        Json::Value item;
        item["session_id"] = sid;
        item["last_access"] = static_cast<Json::Int64>(last_access);
        item["radar_count"] = static_cast<int>(g_scene_mgr.getRadarIds(sid).size());
        item["jammer_count"] = static_cast<int>(g_scene_mgr.getJammerIds(sid).size());
        item["ws_connections"] = static_cast<int>(g_broadcaster.countSessionConnections(sid));
        arr.append(item);
    }

    Json::Value ret;
    ret["success"] = true;
    ret["sessions"] = arr;
    ret["total"] = static_cast<int>(sessions.size());
    return makeResp(makeJson(ret));
}

RouteResponse handleSceneStats(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return makeErr("use GET");

    auto sessions = g_scene_mgr.getAllSessions();
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));

    int recent_1h = 0, recent_6h = 0, recent_24h = 0, older = 0;
    for (const auto& [sid, last_access] : sessions) {
        uint64_t age = now - last_access;
        if (age <= 3600) recent_1h++;
        else if (age <= 21600) recent_6h++;
        else if (age <= 86400) recent_24h++;
        else older++;
    }

    Json::Value stats;
    stats["success"] = true;
    stats["session_count"] = static_cast<int>(g_scene_mgr.getSessionCount());
    stats["total_radars"] = static_cast<int>(g_scene_mgr.getTotalRadarCount());
    stats["total_jammers"] = static_cast<int>(g_scene_mgr.getTotalJammerCount());
    stats["websocket_connections"] = static_cast<int>(g_broadcaster.countConnections());
    stats["session_age_distribution"]["recent_1h"] = recent_1h;
    stats["session_age_distribution"]["recent_6h"] = recent_6h;
    stats["session_age_distribution"]["recent_24h"] = recent_24h;
    stats["session_age_distribution"]["older"] = older;

    size_t mem_estimate = sessions.size() * 1024;
    mem_estimate += g_scene_mgr.getTotalRadarCount() * 256;
    mem_estimate += g_scene_mgr.getTotalJammerCount() * 256;
    stats["memory_estimate_bytes"] = static_cast<Json::Int64>(mem_estimate);

    return makeResp(makeJson(stats));
}

void initRoutes(RouteTrie& trie) {
    trie.addRoute("/", [](const http::request<http::string_body>& req) -> RouteResponse {
        std::string target = svToString(req.target());
        size_t qpos = target.find('?');
        if (qpos != std::string::npos) target = target.substr(0, qpos);
        return serveStaticFile(target);
    });

    trie.addRoute("/api/scene", handleScene);
    trie.addRoute("/api/scene/list", handleSceneList);
    trie.addRoute("/api/scene/stats", handleSceneStats);
    trie.addRoute("/api/radars", handleRadars);
    trie.addRoute("/api/radar", handleRadar);
    trie.addRoute("/api/jammers", handleJammers);
    trie.addRoute("/api/jammer", handleJammer);
    trie.addRoute("/api/simulate", handleSimulate);
    trie.addRoute("/api/dqn/state", handleDqnState);
    trie.addRoute("/api/dqn/action", handleDqnAction);
}
