#include <TcpServer.h>
#include <my_web_app_v1.h>
#include <sence/scene_manager.h>
#include <algo/math_const.h>
#include <jsoncpp/json/json.h>
#include <grpc_gen/agent_service.grpc.pb.h>
#include <grpcpp/grpcpp.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <memory>

static ECMSim::SceneManager g_scene_mgr;
static std::string g_index_html;

// ====== Helper: build JSON response body ======
static std::string jsonOk(const std::string& key, const std::string& val) {
    Json::Value r; r[key] = val; r["success"] = true;
    Json::StyledWriter w; return w.write(r);
}
static std::string jsonOk() { return "{\"success\":true}\n"; }
static std::string jsonError(const std::string& msg) {
    Json::Value r; r["success"] = false; r["error"] = msg;
    Json::StyledWriter w; return w.write(r);
}
static std::string jsonResult(const std::string& body) {
    Json::StyledWriter w; Json::Value r;
    Json::Reader reader;
    reader.parse(body, r);
    Json::Value ret; ret["success"] = true; ret["data"] = r;
    return w.write(ret);
}

// ====== HTTP route handlers ======
extern "C" void* handleIndex(service_element_t*, http_request_context_t* ctx) {
    if (ctx->method != GET) return NULL;
    ctx->resp_body_len = g_index_html.size();
    char* body = (char*)malloc(ctx->resp_body_len + 1);
    if (!body) return NULL;
    memcpy(body, g_index_html.data(), ctx->resp_body_len + 1);
    return body;
}

extern "C" void* handleApiScene(service_element_t*, http_request_context_t* ctx) {
    if (ctx->method != POST || !ctx->content || ctx->content_len <= 0) return NULL;
    std::string req(ctx->content, ctx->content_len);
    Json::Reader reader; Json::Value root;
    if (!reader.parse(req, root, false)) return NULL;

    std::string resp;
    std::string action = root.get("action", "").asString();
    std::string sid    = root.get("session_id", "").asString();

    if (action == "create") {
        std::string new_sid = g_scene_mgr.createSession();
        resp = jsonOk("session_id", new_sid);
    } else if (action == "delete") {
        resp = g_scene_mgr.deleteSession(sid) ? jsonOk() : jsonError("session not found");
    } else if (action == "config") {
        if (!g_scene_mgr.hasSession(sid)) { resp = jsonError("session not found"); }
        else { std::string cfg = g_scene_mgr.getSceneJson(sid); resp = jsonResult(cfg); }
    } else if (action == "load_config") {
        if (!g_scene_mgr.hasSession(sid)) { resp = jsonError("session not found"); }
        else {
            std::string cfgStr = root.get("config", "").asString();
            if (cfgStr.empty()) { resp = jsonError("missing config"); }
            else { resp = g_scene_mgr.loadSceneFromJson(sid, cfgStr) ? jsonOk() : jsonError("parse failed"); }
        }
    } else if (action == "simulate") {
        if (!g_scene_mgr.hasSession(sid)) { resp = jsonError("session not found"); }
        else {
            auto results = g_scene_mgr.runSimulation(sid);
            Json::StyledWriter w; Json::Value arr(Json::arrayValue);
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
                    Json::Value j; j["jammer_id"] = r.jammer_ids[i];
                    j["freq_delta_Hz"] = r.jam_freq_deltas[i];
                    j["freq_match_factor"] = r.freq_match_factors[i];
                    j["effective_power_W"] = r.effective_jam_powers[i];
                    jd.append(j);
                }
                item["jammer_details"] = jd;
                arr.append(item);
            }
            Json::Value ret; ret["success"] = true; ret["data"]["sim_results"] = arr;
            resp = w.write(ret);
        }
    } else if (action == "dqn_state") {
        int jammer_id = root.get("jammer_id", -1).asInt();
        auto state = g_scene_mgr.getStateForJammer(sid, jammer_id);
        if (state.empty()) { resp = jsonError("jammer not found"); }
        else {
            Json::StyledWriter w; Json::Value s(Json::arrayValue);
            for (double v : state) s.append(v);
            Json::Value ret; ret["success"] = true; ret["data"] = s;
            resp = w.write(ret);
        }
    } else if (action == "dqn_action") {
        int jammer_id = root.get("jammer_id", -1).asInt();
        double power = root.get("power_dbm", 0.0).asDouble();
        double freq   = root.get("jam_freq", 0.0).asDouble();
        resp = g_scene_mgr.executeJammerAction(sid, jammer_id, power, freq)
            ? jsonOk() : jsonError("jammer not found");
    } else {
        resp = jsonError("unknown action");
    }

    ctx->resp_body_len = resp.size();
    char* body = (char*)malloc(ctx->resp_body_len + 1);
    if (!body) return NULL;
    memcpy(body, resp.data(), ctx->resp_body_len + 1);
    return body;
}

// Radar CRUD
extern "C" void* handleApiRadar(service_element_t*, http_request_context_t* ctx) {
    if (ctx->method != POST || !ctx->content || ctx->content_len <= 0) return NULL;
    std::string req(ctx->content, ctx->content_len);
    Json::Reader reader; Json::Value root;
    if (!reader.parse(req, root, false)) return NULL;

    std::string resp;
    std::string action = root.get("action", "").asString();
    std::string sid    = root.get("session_id", "").asString();
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonError("session not found"); goto send; }

    if (action == "add") {
        const auto& jr = root["radar"];
        int id = jr["id"].asInt();
        ECMSim::Radar r(id, jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(),
            jr["freq"].asDouble(), jr["bandwidth"].asDouble(),
            jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        resp = g_scene_mgr.addRadar(sid, r) ? jsonOk() : jsonError("add failed");
    } else if (action == "del") {
        int id = root.get("radar_id", -1).asInt();
        resp = g_scene_mgr.removeRadar(sid, id) ? jsonOk() : jsonError("radar not found");
    } else if (action == "update") {
        const auto& jr = root["radar"];
        int id = jr["id"].asInt();
        ECMSim::Radar r(id, jr["x"].asDouble(), jr["y"].asDouble(),
            jr["Pt_dBm"].asDouble(), jr["G_dB"].asDouble(),
            jr["freq"].asDouble(), jr["bandwidth"].asDouble(),
            jr["sigma"].asDouble(), jr["thresh_db"].asDouble());
        resp = g_scene_mgr.updateRadar(sid, id, r) ? jsonOk() : jsonError("radar not found");
    } else {
        resp = jsonError("unknown action");
    }

send:
    ctx->resp_body_len = resp.size();
    char* body = (char*)malloc(ctx->resp_body_len + 1);
    if (!body) return NULL;
    memcpy(body, resp.data(), ctx->resp_body_len + 1);
    return body;
}

extern "C" void* handleApiJammer(service_element_t*, http_request_context_t* ctx) {
    if (ctx->method != POST || !ctx->content || ctx->content_len <= 0) return NULL;
    std::string req(ctx->content, ctx->content_len);
    Json::Reader reader; Json::Value root;
    if (!reader.parse(req, root, false)) return NULL;

    std::string resp;
    std::string action = root.get("action", "").asString();
    std::string sid    = root.get("session_id", "").asString();
    if (!g_scene_mgr.hasSession(sid)) { resp = jsonError("session not found"); goto send; }

    if (action == "add" || action == "update") {
        const auto& jj = root["jammer"];
        int id = jj["id"].asInt();
        ECMSim::JamType jt = jj["jam_type"].asString() == "NOISE_JAM"
            ? ECMSim::JamType::NOISE_JAM : ECMSim::JamType::RANGE_DECEPT;
        ECMSim::Jammer j(id, jj["x"].asDouble(), jj["y"].asDouble(),
            jj["Pj_dBm"].asDouble(), jj["Gj_dB"].asDouble(),
            jj["jam_freq"].asDouble(), jt);
        if (action == "add") resp = g_scene_mgr.addJammer(sid, j) ? jsonOk() : jsonError("add failed");
        else resp = g_scene_mgr.updateJammer(sid, id, j) ? jsonOk() : jsonError("jammer not found");
    } else if (action == "del") {
        int id = root.get("jammer_id", -1).asInt();
        resp = g_scene_mgr.removeJammer(sid, id) ? jsonOk() : jsonError("jammer not found");
    } else {
        resp = jsonError("unknown action");
    }

send:
    ctx->resp_body_len = resp.size();
    char* body = (char*)malloc(ctx->resp_body_len + 1);
    if (!body) return NULL;
    memcpy(body, resp.data(), ctx->resp_body_len + 1);
    return body;
}

// ====== gRPC service implementation ======
class AgentServiceImpl final : public ecmsim::AgentService::Service {
    grpc::Status GetState(grpc::ServerContext*, const ecmsim::StateRequest* req,
                          ecmsim::StateResponse* rep) override {
        auto state = g_scene_mgr.getStateForJammer(req->session_id(), req->jammer_id());
        if (state.empty()) {
            rep->set_success(false);
            rep->set_error("jammer or session not found");
            return grpc::Status::OK;
        }
        for (double v : state) rep->add_state(v);
        rep->set_success(true);
        return grpc::Status::OK;
    }

    grpc::Status ExecuteAction(grpc::ServerContext*, const ecmsim::ActionRequest* req,
                               ecmsim::ActionResponse* rep) override {
        bool ok = g_scene_mgr.executeJammerAction(
            req->session_id(), req->jammer_id(), req->power_dbm(), req->jam_freq());
        rep->set_success(ok);
        if (!ok) rep->set_error("jammer or session not found");
        return grpc::Status::OK;
    }

    grpc::Status StepSimulation(grpc::ServerContext*, const ecmsim::StepRequest* req,
                                ecmsim::StepResponse* rep) override {
        auto results = g_scene_mgr.runSimulation(req->session_id());
        if (results.empty()) {
            rep->set_success(false);
            rep->set_error("session not found or no radars");
            return grpc::Status::OK;
        }
        for (const auto& r : results) {
            auto* proto = rep->add_results();
            proto->set_radar_id(r.radar_id);
            proto->set_sinr_db(r.sinr_db);
            proto->set_detect_success(r.detect_ok);
            proto->set_jam_success_score(r.jam_success_score);
            proto->set_total_effective_jam_power(r.total_effective_jam_power);
            proto->set_jsr_db(r.jsr_db);
            proto->set_is_deception_active(r.is_deception_active);
            proto->set_decept_effect_score(r.decept_effect_score);
            for (double d : r.jam_freq_deltas) proto->add_jam_freq_deltas(d);
            for (double z : r.freq_match_factors) proto->add_freq_match_factors(z);
        }
        rep->set_success(true);
        return grpc::Status::OK;
    }
};

// ====== Main ======
static bool loadIndexHtml(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    std::stringstream ss; ss << ifs.rdbuf();
    g_index_html = ss.str();
    return !g_index_html.empty();
}

int main(int argc, char* argv[]) {
    const char* staticDir = "static";
    if (argc >= 2) staticDir = argv[1];

    std::string indexPath = std::string(staticDir) + "/index.html";
    if (!loadIndexHtml(indexPath)) {
        std::cerr << "Cannot load " << indexPath << "\n";
        return 1;
    }

    // HTTP server
    constexpr size_t ROUTE_COUNT = 4;
    service_element_t* routes[ROUTE_COUNT] = {};
    routes[0] = makeServiceElement((char*)"/", 1, handleIndex);
    routes[1] = makeServiceElement((char*)"/api/scene", 10, handleApiScene);
    routes[2] = makeServiceElement((char*)"/api/radar", 10, handleApiRadar);
    routes[3] = makeServiceElement((char*)"/api/jammer", 11, handleApiJammer);
    for (size_t i = 0; i < ROUTE_COUNT; i++) {
        if (!routes[i]) { std::cerr << "route create failed\n"; return 1; }
    }

    my_web_app_v1_t* app = makeMyWebAppV1(routes, ROUTE_COUNT);
    if (!app) { std::cerr << "makeMyWebAppV1 failed\n"; return 1; }

    my_tcp_server_t* http_server = makeMyTcpServer((char*)"0.0.0.0", 8080);
    if (!http_server) { std::cerr << "HTTP server failed\n"; deleteMyWebAppV1(app); return 1; }
    myTcpServerSetCliSkReadCbArgs(http_server,
        makeCliSkReadCbArgs(myWebAppV1MakeResponse, app));

    // Start HTTP server in background thread
    std::thread http_thr([http_server]() {
        std::cout << "HTTP server on :8080\n";
        myTcpServerStart(http_server);
    });

    // gRPC server
    std::string grpc_addr = "0.0.0.0:50051";
    AgentServiceImpl agent_service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(grpc_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&agent_service);
    std::unique_ptr<grpc::Server> grpc_server(builder.BuildAndStart());
    if (!grpc_server) {
        std::cerr << "gRPC server failed on " << grpc_addr << "\n";
        return 1;
    }
    std::cout << "gRPC server on :50051\n";

    grpc_server->Wait();

    http_thr.join();
    deleteMyTcpServer(http_server);
    deleteMyWebAppV1(app);
    return 0;
}