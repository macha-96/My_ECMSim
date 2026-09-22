#include <app/http_handlers.h>
#include <app/common.h>
#include <app/ws_session.h>
#include <sence/scene_manager.h>
#include <jsoncpp/json/json.h>

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
            if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
            if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }
            Json::Value v;
            Json::Reader rd;
            rd.parse(g_scene_mgr.getSceneJson(sid), v);
            v["success"] = true;
            v["session_id"] = sid;
            resp = makeJson(v);
        } else if (action == "delete") {
            sid = body["session_id"].asString();
            if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
            resp = g_scene_mgr.deleteSession(sid) ? jsonOk() : jsonErr("session not found");
        } else {
            resp = jsonErr("unknown action");
        }
    } else if (req.method() == http::verb::get) {
        sid = body["session_id"].asString();
        if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
        if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }
        Json::Value v;
        Json::Reader rd;
        rd.parse(g_scene_mgr.getSceneJson(sid), v);
        v["success"] = true;
        v["session_id"] = sid;
        resp = makeJson(v);
    } else if (req.method() == http::verb::delete_) {
        sid = body["session_id"].asString();
        if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
        resp = g_scene_mgr.deleteSession(sid) ? jsonOk() : jsonErr("session not found");
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

    if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }

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
        resp = makeJson(ret);
    } else if (req.method() == http::verb::post) {
        auto jr = body["radar"];
        if (jr.isNull()) { resp = jsonErr("need radar object"); goto send; }
        ECMSim::Radar rad(
            jr["id"].asInt(), jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
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

std::string handleJammers(const http::request<http::string_body>& req) {
    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    std::string resp;

    if (sid.empty()) { resp = jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonErr("session not found"); goto send; }

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
        resp = makeJson(ret);
    } else if (req.method() == http::verb::post) {
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

std::string handleSimulate(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) {
        return jsonErr("use POST");
    }

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    if (sid.empty()) return jsonErr("need session_id");
    if (!g_scene_mgr.hasSession(sid)) return jsonErr("session not found");

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

std::string handleDqnAction(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) return jsonErr("use POST");

    auto body = parseJson(svToString(req.body()));
    std::string sid = body["session_id"].asString();
    int jid = body["jammer_id"].asInt();
    double pd = body["power_dbm"].asDouble();
    double fq = body["jam_freq"].asDouble();
    if (sid.empty() || !jid) return jsonErr("need session_id and jammer_id");

    bool ok = g_scene_mgr.executeJammerAction(sid, jid, pd, fq);
    if (ok) broadcastScene(sid);
    return ok ? jsonOk() : jsonErr("jammer not found");
}

std::string handleSceneList(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return jsonErr("use GET");

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
    return makeJson(ret);
}

std::string handleSceneStats(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return jsonErr("use GET");

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

    return makeJson(stats);
}
