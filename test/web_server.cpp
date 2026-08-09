/***
 * ECMSim Web Server
 *
 * Integrates the my-reactor-server HTTP library with the ECMSim simulation engine.
 * Serves an embedded single-page web GUI at http://localhost:8080/
 *
 * API endpoints:
 *   GET  /api/scene     — return scene config as JSON
 *   POST /api/scene     — update scene from JSON body, return updated config
 *   POST /api/simulate  — run one simulation step (optionally with body config), return results
 */

#include <my_http_lib/TcpServer.h>
#include <my_http_lib/my_web_app_v1.h>
#include <sence/sim_scene.h>
#include <algo/math_const.h>
#include <jsoncpp/json/json.h>
#include <my_http_lib/MySimpleServerLog.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <mutex>
#include <vector>

/* ------------------------------------------------------------------ */
/*  Global simulation state                                            */
/* ------------------------------------------------------------------ */
static ECMSim::SimScene g_scene;
static std::mutex g_scene_mtx;

/* ------------------------------------------------------------------ */
/*  Helper: load scene from JSON string                                */
/* ------------------------------------------------------------------ */
static ECMSim::JamType jamTypeFromStr(const std::string &s) {
    if (s == "NOISE_JAM")   return ECMSim::JamType::NOISE_JAM;
    if (s == "RANGE_DECEPT") return ECMSim::JamType::RANGE_DECEPT;
    return ECMSim::JamType::NOISE_JAM;
}

static bool loadSceneFromJsonString(const std::string &jsonStr) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(jsonStr, root, false)) return false;

    ECMSim::SimScene new_scene;

    const Json::Value &radars = root["radars"];
    for (const auto &r : radars) {
        ECMSim::Radar rad(
            r["id"].asInt(),
            r["x"].asDouble(),
            r["y"].asDouble(),
            r["Pt_dBm"].asDouble(),
            r["G_dB"].asDouble(),
            r["freq"].asDouble(),
            r["bandwidth"].asDouble(),
            r["sigma"].asDouble(),
            r["thresh_db"].asDouble()
        );
        new_scene.addRadar(rad);
    }

    const Json::Value &jammers = root["jammers"];
    for (const auto &j : jammers) {
        ECMSim::Jammer jam(
            j["id"].asInt(),
            j["x"].asDouble(),
            j["y"].asDouble(),
            j["Pj_dBm"].asDouble(),
            j["Gj_dB"].asDouble(),
            j["jam_freq"].asDouble(),
            jamTypeFromStr(j["jam_type"].asString())
        );
        new_scene.addJammer(jam);
    }

    std::lock_guard<std::mutex> lock(g_scene_mtx);
    g_scene = new_scene;
    return true;
}

/* We keep a shadow of the last-known scene JSON to echo it back. */
static std::string g_last_scene_json;

/* Overloaded loader that also remembers the JSON. */
static bool loadSceneFromStringWithShadow(const std::string &jsonStr) {
    if (!loadSceneFromJsonString(jsonStr)) return false;
    g_last_scene_json = jsonStr;
    return true;
}

static std::string getSceneJson() {
    std::lock_guard<std::mutex> lock(g_scene_mtx);
    return g_last_scene_json;
}

/* ------------------------------------------------------------------ */
/*  Helper: run simulation and return JSON result string               */
/* ------------------------------------------------------------------ */
static std::string runSimulation() {
    std::lock_guard<std::mutex> lock(g_scene_mtx);
    std::vector<ECMSim::RadarSimResult> results = g_scene.runOneStep();

    Json::Value root;
    Json::Value arr(Json::arrayValue);
    for (const auto &r : results) {
        Json::Value item;
        item["radar_id"]          = r.radar_id;
        item["signal_power_W"]    = r.signal_power;
        item["total_jam_power_W"] = r.total_jam_power;
        item["noise_power_W"]     = r.noise_power;
        item["SINR_lin"]          = r.sinr_lin;
        item["SINR_dB"]           = r.sinr_db;
        item["detect_success"]    = r.detect_ok;

        // 多维度评估新字段
        item["total_effective_jam_power_W"] = r.total_effective_jam_power;
        item["jsr_lin"]     = r.jsr_lin;
        item["jsr_dB"]      = r.jsr_db;
        item["jam_success_score"] = r.jam_success_score;
        item["is_deception_active"] = r.is_deception_active;
        item["decept_effect_score"] = r.decept_effect_score;

        // 各干扰机明细
        Json::Value jamDetails(Json::arrayValue);
        for (size_t i = 0; i < r.jammer_ids.size(); i++) {
            Json::Value jd;
            jd["jammer_id"]            = r.jammer_ids[i];
            jd["freq_delta_Hz"]        = r.jam_freq_deltas[i];
            jd["freq_match_factor"]    = r.freq_match_factors[i];
            jd["original_power_W"]     = r.original_jam_powers[i];
            jd["effective_power_W"]    = r.effective_jam_powers[i];
            jamDetails.append(jd);
        }
        item["jammer_details"] = jamDetails;

        arr.append(item);
    }
    root["sim_results"] = arr;

    Json::StyledWriter writer;
    return writer.write(root);
}

/* ------------------------------------------------------------------ */
/*  Static file cache                                                  */
/* ------------------------------------------------------------------ */
static std::string g_index_html;

static bool loadIndexHtml(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    std::stringstream ss;
    ss << ifs.rdbuf();
    g_index_html = ss.str();
    return !g_index_html.empty();
}

/* ------------------------------------------------------------------ */
/*  HTTP Route Handlers                                                */
/* ------------------------------------------------------------------ */

extern "C" void* handleIndex(service_element_t *self, http_request_context_t *ctx) {
    (void)self;
    if (ctx->method != GET) return NULL;
    ctx->resp_body_len = g_index_html.size();
    char *body = (char*)malloc(ctx->resp_body_len + 1);
    if (!body) return NULL;
    memcpy(body, g_index_html.data(), ctx->resp_body_len + 1);
    LOG_INFO("GET /, status 200");
    return body;
}

extern "C" void* handleApiScene(service_element_t *self, http_request_context_t *ctx) {
    (void)self;

    if (ctx->method == GET) {
        std::string json = getSceneJson();
        if (json.empty()) json = "{}";
        ctx->resp_body_len = json.size();
        char *body = (char*)malloc(ctx->resp_body_len + 1);
        if (!body) return NULL;
        memcpy(body, json.data(), ctx->resp_body_len + 1);
        LOG_INFO("GET /api/scene, status 200");
        return body;
    }

    if (ctx->method == POST && ctx->content && ctx->content_len > 0) {
        std::string bodyStr(ctx->content, ctx->content_len);
        if (!loadSceneFromStringWithShadow(bodyStr)) {
            ctx->resp_body_len = 0;
            LOG_ERROR("POST /api/scene, status 500");
            return NULL;  /* 500 */
        }
        std::string json = getSceneJson();
        if (json.empty()) json = bodyStr;  /* fallback */
        ctx->resp_body_len = json.size();
        char *body = (char*)malloc(ctx->resp_body_len + 1);
        if (!body) return NULL;
        memcpy(body, json.data(), ctx->resp_body_len + 1);
        LOG_INFO("POST /api/scene, status 200");
        return body;
    }
    
    LOG_ERROR("POST /api/scene, status 404");
    return NULL; /* 404 / 405 */
}

extern "C" void* handleApiSimulate(service_element_t *self, http_request_context_t *ctx) {
    (void)self;

    /* Accept both GET and POST.  POST body may contain updated config. */
    if (ctx->method == POST && ctx->content && ctx->content_len > 0) {
        std::string bodyStr(ctx->content, ctx->content_len);
        loadSceneFromStringWithShadow(bodyStr);
    }

    std::string json = runSimulation();
    ctx->resp_body_len = json.size();
    char *body = (char*)malloc(ctx->resp_body_len + 1);
    if (!body) return NULL;
    memcpy(body, json.data(), ctx->resp_body_len + 1);
    LOG_INFO("GET /api/simulate, status 200");
    return body;
}

/* ------------------------------------------------------------------ */
/*  Main entry point                                                   */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    /* Default paths */
    const char *cfgPath   = "config/sence_config.json";
    const char *staticDir = "static";

    if (argc >= 2) cfgPath   = argv[1];
    if (argc >= 3) staticDir = argv[2];

    /* Load static index.html from the static directory */
    std::string indexPath = std::string(staticDir) + "/index.html";
    if (!loadIndexHtml(indexPath)) {
        std::cerr << "Cannot load static/index.html from " << indexPath << "\n";
        std::cerr << "Usage: " << argv[0] << " [config_path] [static_dir]\n";
        return 1;
    }

    std::ifstream ifs(cfgPath);
    if (!ifs.is_open()) {
        std::cerr << "Cannot open config: " << cfgPath << "\n";
        std::cerr << "Usage: " << argv[0] << " [config_path] [static_dir]\n";
        return 1;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string cfgStr = ss.str();
    ifs.close();

    if (!loadSceneFromStringWithShadow(cfgStr)) {
        std::cerr << "Failed to parse config JSON\n";
        return 1;
    }
    std::cout << "Loaded scene config from " << cfgPath << "\n";

    /* Build service elements */
    constexpr size_t ROUTE_COUNT = 3;
    service_element_t *routes[ROUTE_COUNT] = {nullptr};

    routes[0] = makeServiceElement((char*)"/", strlen("/"), handleIndex);
    if (!routes[0]) { std::cerr << "makeServiceElement(/) failed\n"; return 1; }

    routes[1] = makeServiceElement((char*)"/api/scene", strlen("/api/scene"), handleApiScene);
    if (!routes[1]) { std::cerr << "makeServiceElement(/api/scene) failed\n"; deleteServiceElement(routes[0]); return 1; }

    routes[2] = makeServiceElement((char*)"/api/simulate", strlen("/api/simulate"), handleApiSimulate);
    if (!routes[2]) { std::cerr << "makeServiceElement(/api/simulate) failed\n"; deleteServiceElement(routes[0]); deleteServiceElement(routes[1]); return 1; }

    /* Create web app */
    my_web_app_v1_t *app = makeMyWebAppV1(routes, ROUTE_COUNT);
    if (!app) {
        std::cerr << "makeMyWebAppV1 failed\n";
        for (auto *r : routes) if (r) deleteServiceElement(r);
        return 1;
    }
    // app owns the routes now — do NOT manually free them

    /* Create TCP server */
    my_tcp_server_t *server = makeMyTcpServer((char*)"0.0.0.0", 8080);
    if (!server) {
        std::cerr << "makeMyTcpServer failed\n";
        deleteMyWebAppV1(app);  // frees routes internally
        return 1;
    }

    myTcpServerSetCliSkReadCbArgs(server,
        makeCliSkReadCbArgs(myWebAppV1MakeResponse, app));

    std::cout << "ECMSim Web Server running at http://localhost:8088\n";
    myTcpServerStart(server);

    /* Cleanup — deleteMyWebAppV1 frees the trie and all route nodes */
    deleteMyTcpServer(server);
    deleteMyWebAppV1(app);

    return 0;
}
