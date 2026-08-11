/***
 * ECMSim V3 — RESTful API + gRPC
 * Routes (exact match for trie router):
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
 * HTTP :8080, gRPC :50051
 */

#include <TcpServer.h>
#include <my_web_app_v1.h>
#include <sence/scene_manager.h>
#include <algo/math_const.h>
#include <jsoncpp/json/json.h>
#include <grpc_gen/agent_service.grpc.pb.h>
#include <grpcpp/grpcpp.h>
#include <my_http_lib/MySimpleServerLog.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <memory>

static ECMSim::SceneManager g_scene_mgr;
static std::string g_index_html;

/* JSON helpers */
static std::string makeJson(const Json::Value& v) {
    Json::StyledWriter w; return w.write(v);
}
static std::string jsonOk() { return "{\"success\":true}\n"; }
static std::string jsonErr(const std::string& msg) {
    Json::Value r; r["success"]=false; r["error"]=msg; return makeJson(r);
}

/* Parse body */
static Json::Value parseBody(http_request_context_t* ctx) {
    Json::Value r; if (!ctx->content || ctx->content_len<=0) return r;
    Json::Reader rd; rd.parse(std::string(ctx->content, ctx->content_len), r, false); return r;
}

/* Allocate response and return C string */
static char* allocResp(const std::string& json, http_request_context_t* ctx) {
    ctx->resp_body_len = json.size();
    char* b = (char*)malloc(ctx->resp_body_len+1);
    if (!b) return NULL; memcpy(b, json.data(), ctx->resp_body_len+1); return b;
}

/* ====== Handlers ====== */
extern "C" void* hIndex(service_element_t*, http_request_context_t* ctx) {
    if (ctx->method != GET) return NULL;
    LOG_INFO("GET /");
    return allocResp(g_index_html, ctx);
}

extern "C" void* hScene(service_element_t*, http_request_context_t* ctx) {
    LOG_INFO("%s /api/scene", ctx->method==POST?"POST":ctx->method==GET?"GET":"DELETE");
    auto body = parseBody(ctx); std::string sid, action; std::string resp;
    if (ctx->method == POST) {
        action = body.get("action", "").asString();
        if (action.empty()) {
            /* POST without action → create session */
            sid = g_scene_mgr.createSession();
            Json::Value r; r["success"]=true; r["session_id"]=sid; resp=makeJson(r);
        } else if (action == "config") {
            /* POST with action=config → return scene config */
            sid = body["session_id"].asString();
            if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
            if (!g_scene_mgr.hasSession(sid)) { resp=jsonErr("session not found"); goto send; }
            Json::Value v; Json::Reader rd; rd.parse(g_scene_mgr.getSceneJson(sid), v);
            v["success"]=true; v["session_id"]=sid; resp=makeJson(v);
        } else if (action == "delete") {
            /* POST with action=delete → delete session */
            sid = body["session_id"].asString();
            if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
            resp = g_scene_mgr.deleteSession(sid) ? jsonOk() : jsonErr("session not found");
        } else resp=jsonErr("unknown action");
    } else if (ctx->method == GET) {
        sid=body["session_id"].asString();
        if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
        if (!g_scene_mgr.hasSession(sid)) { resp=jsonErr("session not found"); goto send; }
        Json::Value v; Json::Reader rd; rd.parse(g_scene_mgr.getSceneJson(sid), v);
        v["success"]=true; v["session_id"]=sid; resp=makeJson(v);
    } else if (ctx->method == DELETE) {
        sid=body["session_id"].asString();
        if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
        resp = g_scene_mgr.deleteSession(sid) ? jsonOk() : jsonErr("session not found");
    } else resp=jsonErr("use POST/GET/DELETE");
send: return allocResp(resp, ctx);
}

extern "C" void* hRadars(service_element_t*, http_request_context_t* ctx) {
    LOG_INFO("%s /api/radars", ctx->method==GET?"GET":"POST");
    auto body = parseBody(ctx); std::string sid=body["session_id"].asString(); std::string resp;
    if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp=jsonErr("session not found"); goto send; }
    if (ctx->method == GET) {
        Json::Value arr(Json::arrayValue);
        for (int id : g_scene_mgr.getRadarIds(sid)) {
            auto* r = g_scene_mgr.getRadar(sid, id); if (!r) continue;
            Json::Value j; j["id"]=r->getId(); j["x"]=r->getPos().first; j["y"]=r->getPos().second;
            j["Pt_dBm"]=10*log10(r->getPtLin()*1000); j["G_dB"]=10*log10(r->getGLin());
            j["freq"]=r->getFreq(); j["bandwidth"]=r->getBandwidth();
            j["sigma"]=r->getRCS(); j["thresh_db"]=10*log10(r->getThreshLin());
            arr.append(j);
        }
        Json::Value ret; ret["success"]=true; ret["radars"]=arr; resp=makeJson(ret);
    } else if (ctx->method == POST) {
        auto jr = body["radar"]; if (jr.isNull()) { resp=jsonErr("need radar object"); goto send; }
        ECMSim::Radar rad(jr["id"].asInt(), jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        resp = g_scene_mgr.addRadar(sid, rad) ? jsonOk() : jsonErr("add failed");
    } else resp=jsonErr("use GET/POST");
send: return allocResp(resp, ctx);
}

extern "C" void* hRadar(service_element_t*, http_request_context_t* ctx) {
    auto body = parseBody(ctx); std::string sid=body["session_id"].asString(); std::string action, resp;
    LOG_INFO("%s /api/radar", ctx->method==GET?"GET":ctx->method==PUT?"PUT":ctx->method==DELETE?"DELETE":"POST");
    if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp=jsonErr("session not found"); goto send; }

    if (ctx->method == POST) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int rid=body["radar_id"].asInt(); if(!rid){resp=jsonErr("need radar_id");goto send;}
            resp = g_scene_mgr.removeRadar(sid, rid) ? jsonOk() : jsonErr("not found");
        } else if (action == "update") {
            auto jr=body["radar"]; if(jr.isNull()){resp=jsonErr("need radar");goto send;}
            int rid=jr["id"].asInt();
            ECMSim::Radar rad(rid, jr["x"].asDouble(), jr["y"].asDouble(),
                jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
                jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
            resp = g_scene_mgr.updateRadar(sid, rid, rad) ? jsonOk() : jsonErr("update failed");
        } else resp=jsonErr("unknown action");
    } else if (ctx->method==GET || ctx->method==DELETE) {
        int rid=body["radar_id"].asInt(); if(!rid){resp=jsonErr("need radar_id");goto send;}
        if (ctx->method==GET) {
            auto* r=g_scene_mgr.getRadar(sid, rid); if(!r){resp=jsonErr("not found");goto send;}
            Json::Value j; j["success"]=true; j["radar_id"]=rid;
            j["x"]=r->getPos().first; j["y"]=r->getPos().second;
            j["Pt_dBm"]=10*log10(r->getPtLin()*1000); j["G_dB"]=10*log10(r->getGLin());
            j["freq"]=r->getFreq(); j["bandwidth"]=r->getBandwidth();
            j["sigma"]=r->getRCS(); j["thresh_db"]=10*log10(r->getThreshLin());
            resp=makeJson(j);
        } else resp = g_scene_mgr.removeRadar(sid, rid) ? jsonOk() : jsonErr("not found");
    } else if (ctx->method == PUT) {
        auto jr=body["radar"]; if(jr.isNull()){resp=jsonErr("need radar");goto send;}
        int rid=jr["id"].asInt();
        ECMSim::Radar rad(rid, jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(), jr["freq"].asDouble(),
            jr["bandwidth"].asDouble(), jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        resp = g_scene_mgr.updateRadar(sid, rid, rad) ? jsonOk() : jsonErr("update failed");
    } else resp=jsonErr("use POST/GET/PUT/DELETE");
send: return allocResp(resp, ctx);
}

extern "C" void* hJammers(service_element_t*, http_request_context_t* ctx) {
    LOG_INFO("%s /api/jammers", ctx->method==GET?"GET":"POST");
    auto body = parseBody(ctx); std::string sid=body["session_id"].asString(); std::string resp;
    if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp=jsonErr("session not found"); goto send; }
    if (ctx->method == GET) {
        Json::Value arr(Json::arrayValue);
        for (int jid : g_scene_mgr.getJammerIds(sid)) {
            auto* j = g_scene_mgr.getJammer(sid, jid); if (!j) continue;
            Json::Value jv; jv["id"]=j->getId(); jv["x"]=j->getPos().first; jv["y"]=j->getPos().second;
            jv["Pj_dBm"]=10*log10(j->getPjLin()*1000); jv["Gj_dB"]=10*log10(j->getGjLin());
            jv["jam_freq"]=j->getJamFreq();
            jv["jam_type"]=j->getJamType()==ECMSim::JamType::NOISE_JAM?"NOISE_JAM":"RANGE_DECEPT";
            arr.append(jv);
        }
        Json::Value ret; ret["success"]=true; ret["jammers"]=arr; resp=makeJson(ret);
    } else if (ctx->method == POST) {
        auto jv=body["jammer"]; if(jv.isNull()){resp=jsonErr("need jammer"); goto send;}
        ECMSim::JamType jt=jv["jam_type"].asString()=="NOISE_JAM"?ECMSim::JamType::NOISE_JAM:ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(jv["id"].asInt(), jv["x"].asDouble(), jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(), jv["jam_freq"].asDouble(), jt);
        resp = g_scene_mgr.addJammer(sid, jam) ? jsonOk() : jsonErr("add failed");
    } else resp=jsonErr("use GET/POST");
send: return allocResp(resp, ctx);
}

extern "C" void* hJammer(service_element_t*, http_request_context_t* ctx) {
    auto body = parseBody(ctx); std::string sid=body["session_id"].asString(); std::string action, resp;
    LOG_INFO("%s /api/jammer", ctx->method==GET?"GET":ctx->method==PUT?"PUT":ctx->method==DELETE?"DELETE":"POST");
    if (sid.empty()) { resp=jsonErr("need session_id"); goto send; }
    if (!g_scene_mgr.hasSession(sid)) { resp=jsonErr("session not found"); goto send; }

    if (ctx->method == POST) {
        action = body.get("action", "").asString();
        if (action == "del") {
            int jid=body["jammer_id"].asInt(); if(!jid){resp=jsonErr("need jammer_id");goto send;}
            resp = g_scene_mgr.removeJammer(sid, jid) ? jsonOk() : jsonErr("not found");
        } else if (action == "update") {
            auto jv=body["jammer"]; if(jv.isNull()){resp=jsonErr("need jammer");goto send;}
            int jid=jv["id"].asInt();
            ECMSim::JamType jt=jv["jam_type"].asString()=="NOISE_JAM"?ECMSim::JamType::NOISE_JAM:ECMSim::JamType::RANGE_DECEPT;
            ECMSim::Jammer jam(jid, jv["x"].asDouble(), jv["y"].asDouble(),
                jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(), jv["jam_freq"].asDouble(), jt);
            resp = g_scene_mgr.updateJammer(sid, jid, jam) ? jsonOk() : jsonErr("update failed");
        } else resp=jsonErr("unknown action");
    } else if (ctx->method==GET || ctx->method==DELETE) {
        int jid=body["jammer_id"].asInt(); if(!jid){resp=jsonErr("need jammer_id");goto send;}
        if (ctx->method==GET) {
            auto* j=g_scene_mgr.getJammer(sid, jid); if(!j){resp=jsonErr("not found");goto send;}
            Json::Value jv; jv["success"]=true; jv["jammer_id"]=jid;
            jv["x"]=j->getPos().first; jv["y"]=j->getPos().second;
            jv["Pj_dBm"]=10*log10(j->getPjLin()*1000); jv["Gj_dB"]=10*log10(j->getGjLin());
            jv["jam_freq"]=j->getJamFreq();
            jv["jam_type"]=j->getJamType()==ECMSim::JamType::NOISE_JAM?"NOISE_JAM":"RANGE_DECEPT";
            resp=makeJson(jv);
        } else resp = g_scene_mgr.removeJammer(sid, jid) ? jsonOk() : jsonErr("not found");
    } else if (ctx->method == PUT) {
        auto jv=body["jammer"]; if(jv.isNull()){resp=jsonErr("need jammer");goto send;}
        int jid=jv["id"].asInt();
        ECMSim::JamType jt=jv["jam_type"].asString()=="NOISE_JAM"?ECMSim::JamType::NOISE_JAM:ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer jam(jid, jv["x"].asDouble(), jv["y"].asDouble(),
            jv["Pj_dBm"].asDouble(), jv["Gj_dB"].asDouble(), jv["jam_freq"].asDouble(), jt);
        resp = g_scene_mgr.updateJammer(sid, jid, jam) ? jsonOk() : jsonErr("update failed");
    } else resp=jsonErr("use POST/GET/PUT/DELETE");
send: return allocResp(resp, ctx);
}

extern "C" void* hSim(service_element_t*, http_request_context_t* ctx) {
    LOG_INFO("POST /api/simulate");
    if (ctx->method!=POST) return allocResp(jsonErr("use POST"), ctx);
    auto body=parseBody(ctx); std::string sid=body["session_id"].asString();
    if (sid.empty()) return allocResp(jsonErr("need session_id"), ctx);
    if (!g_scene_mgr.hasSession(sid)) return allocResp(jsonErr("session not found"), ctx);
    auto results = g_scene_mgr.runSimulation(sid);
    Json::Value arr(Json::arrayValue);
    for (const auto& r : results) {
        Json::Value item;
        item["radar_id"]=r.radar_id; item["SINR_dB"]=r.sinr_db;
        item["signal_power_W"]=r.signal_power; item["total_effective_jam_power_W"]=r.total_effective_jam_power;
        item["jsr_dB"]=r.jsr_db; item["detect_success"]=r.detect_ok;
        item["jam_success_score"]=r.jam_success_score;
        item["is_deception_active"]=r.is_deception_active; item["decept_effect_score"]=r.decept_effect_score;
        Json::Value jd(Json::arrayValue);
        for (size_t i=0;i<r.jammer_ids.size();i++) {
            Json::Value j; j["jammer_id"]=r.jammer_ids[i]; j["freq_delta_Hz"]=r.jam_freq_deltas[i];
            j["freq_match_factor"]=r.freq_match_factors[i]; j["effective_power_W"]=r.effective_jam_powers[i];
            jd.append(j);
        }
        item["jammer_details"]=jd; arr.append(item);
    }
    Json::Value ret; ret["success"]=true; ret["sim_results"]=arr;
    return allocResp(makeJson(ret), ctx);
}

extern "C" void* hDqnState(service_element_t*, http_request_context_t* ctx) {
    LOG_INFO("GET /api/dqn/state");
    if (ctx->method!=GET) return allocResp(jsonErr("use GET"), ctx);
    auto body=parseBody(ctx); std::string sid=body["session_id"].asString();
    int jid=body["jammer_id"].asInt();
    if (sid.empty()||!jid) return allocResp(jsonErr("need session_id and jammer_id"), ctx);
    auto state=g_scene_mgr.getStateForJammer(sid, jid);
    if (state.empty()) return allocResp(jsonErr("jammer not found"), ctx);
    Json::Value s(Json::arrayValue); for (double v:state) s.append(v);
    Json::Value ret; ret["success"]=true; ret["state"]=s;
    return allocResp(makeJson(ret), ctx);
}

extern "C" void* hDqnAction(service_element_t*, http_request_context_t* ctx) {
    LOG_INFO("POST /api/dqn/action");
    if (ctx->method!=POST) return allocResp(jsonErr("use POST"), ctx);
    auto body=parseBody(ctx); std::string sid=body["session_id"].asString();
    int jid=body["jammer_id"].asInt(); double pd=body["power_dbm"].asDouble(); double fq=body["jam_freq"].asDouble();
    if (sid.empty()||!jid) return allocResp(jsonErr("need session_id and jammer_id"), ctx);
    bool ok=g_scene_mgr.executeJammerAction(sid, jid, pd, fq);
    return allocResp(ok?jsonOk():jsonErr("jammer not found"), ctx);
}

/* ====== gRPC ====== */
class AgentSvc final : public ecmsim::AgentService::Service {
    grpc::Status GetState(grpc::ServerContext*, const ecmsim::StateRequest* rq, ecmsim::StateResponse* rp) override {
        auto s=g_scene_mgr.getStateForJammer(rq->session_id(), rq->jammer_id());
        if(s.empty()){rp->set_success(false);rp->set_error("not found");return grpc::Status::OK;}
        for(double v:s)rp->add_state(v); rp->set_success(true); return grpc::Status::OK;
    }
    grpc::Status ExecuteAction(grpc::ServerContext*, const ecmsim::ActionRequest* rq, ecmsim::ActionResponse* rp) override {
        bool ok=g_scene_mgr.executeJammerAction(rq->session_id(), rq->jammer_id(), rq->power_dbm(), rq->jam_freq());
        rp->set_success(ok); if(!ok)rp->set_error("not found"); return grpc::Status::OK;
    }
    grpc::Status StepSimulation(grpc::ServerContext*, const ecmsim::StepRequest* rq, ecmsim::StepResponse* rp) override {
        auto rs=g_scene_mgr.runSimulation(rq->session_id());
        if(rs.empty()){rp->set_success(false);rp->set_error("no radars");return grpc::Status::OK;}
        for(const auto& r:rs){
            auto* p=rp->add_results(); p->set_radar_id(r.radar_id); p->set_sinr_db(r.sinr_db);
            p->set_detect_success(r.detect_ok); p->set_jam_success_score(r.jam_success_score);
            p->set_total_effective_jam_power(r.total_effective_jam_power); p->set_jsr_db(r.jsr_db);
            p->set_is_deception_active(r.is_deception_active); p->set_decept_effect_score(r.decept_effect_score);
            for(double d:r.jam_freq_deltas)p->add_jam_freq_deltas(d);
            for(double z:r.freq_match_factors)p->add_freq_match_factors(z);
        }
        rp->set_success(true); return grpc::Status::OK;
    }
};

/* ====== Main ====== */
static bool loadFile(const std::string& p, std::string& out) {
    std::ifstream f(p); if(!f.is_open())return false;
    std::stringstream ss; ss<<f.rdbuf(); out=ss.str(); return !out.empty();
}

int main(int argc, char* argv[]) {
    const char* staticDir = argc>=2 ? argv[1] : "static";
    std::string indexPath = std::string(staticDir)+"/index_v2.html";
    if (!loadFile(indexPath, g_index_html)) {
        // Fallback to index.html
        indexPath = std::string(staticDir)+"/index.html";
        if (!loadFile(indexPath, g_index_html)) {
            std::cerr<<"[ERR] Cannot load index.html\n"; return 1;
        }
    }
    std::cout<<"[INFO] Loaded "<<indexPath<<" ("<<g_index_html.size()<<" bytes)\n";

    constexpr size_t N=9;
    service_element_t* routes[N]={};
    struct{const char* p;size_t l;void*(*h)(service_element_t*,http_request_context_t*);} rd[]={
        {"/",1,hIndex},{"/api/scene",10,hScene},{"/api/radars",11,hRadars},{"/api/radar",10,hRadar},
        {"/api/jammers",12,hJammers},{"/api/jammer",11,hJammer},{"/api/simulate",13,hSim},
        {"/api/dqn/state",14,hDqnState},{"/api/dqn/action",14,hDqnAction},
    };
    static_assert(sizeof(rd)/sizeof(rd[0])==N,"route count mismatch");
    for(size_t i=0;i<N;i++){
        routes[i]=makeServiceElement((char*)rd[i].p, rd[i].l, rd[i].h);
        if(!routes[i]){std::cerr<<"[ERR] route "<<rd[i].p<<" failed\n"; return 1;}
    }

    auto* app=makeMyWebAppV1(routes,N); if(!app){std::cerr<<"[ERR] app failed\n"; return 1;}
    auto* srv=makeMyTcpServer((char*)"0.0.0.0",8080);
    if(!srv){std::cerr<<"[ERR] HTTP server failed\n";deleteMyWebAppV1(app);return 1;}
    myTcpServerSetCliSkReadCbArgs(srv,makeCliSkReadCbArgs(myWebAppV1MakeResponse,app));

    std::thread httpThr([srv](){std::cout<<"[INFO] HTTP :8080\n";myTcpServerStart(srv);});

    AgentSvc agentSvc;
    grpc::ServerBuilder gb; gb.AddListeningPort("0.0.0.0:50051",grpc::InsecureServerCredentials());
    gb.RegisterService(&agentSvc);
    auto gs=gb.BuildAndStart();
    if(!gs){std::cerr<<"[ERR] gRPC failed\n"; return 1;}
    std::cout<<"[INFO] gRPC :50051\n";

    gs->Wait(); httpThr.join();
    deleteMyTcpServer(srv); deleteMyWebAppV1(app);
    return 0;
}