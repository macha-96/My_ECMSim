#include <sence/scene_manager.h>
#include <algo/radar_eq.h>
#include <jsoncpp/json/json.h>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace ECMSim {

SceneManager::SceneManager() {}

std::string SceneManager::generateId() {
    uint64_t n = ++m_counter;
    std::stringstream ss;
    ss << "session_" << std::hex << n;
    return ss.str();
}

SessionData* SceneManager::getSession(const std::string& sid) {
    auto it = m_sessions.find(sid);
    return it != m_sessions.end() ? it->second.get() : nullptr;
}

SessionData* SceneManager::getSession(const std::string& sid) const {
    auto it = m_sessions.find(sid);
    return it != m_sessions.end() ? it->second.get() : nullptr;
}

std::string SceneManager::createSession() {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto data = std::make_unique<SessionData>();
    data->created_at = static_cast<uint64_t>(std::time(nullptr));
    data->last_access = data->created_at;
    std::string sid = generateId();
    m_sessions[sid] = std::move(data);
    return sid;
}

bool SceneManager::deleteSession(const std::string& sid) {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_sessions.erase(sid) > 0;
}

bool SceneManager::hasSession(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_sessions.find(sid) != m_sessions.end();
}

bool SceneManager::addRadar(const std::string& sid, const Radar& r) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->radars.insert_or_assign(r.getId(), r);
	    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

bool SceneManager::removeRadar(const std::string& sid, int id) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return s->radars.erase(id) > 0;
}

bool SceneManager::updateRadar(const std::string& sid, int id, const Radar& r) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    auto it = s->radars.find(id);
    if (it == s->radars.end()) return false;
    it->second = r;
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

const Radar* SceneManager::getRadar(const std::string& sid, int id) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return nullptr;
    auto it = s->radars.find(id);
    return it != s->radars.end() ? &it->second : nullptr;
}

std::vector<int> SceneManager::getRadarIds(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return {};
    std::vector<int> ids;
    for (const auto& [k, v] : s->radars) ids.push_back(k);
    return ids;
}

bool SceneManager::addJammer(const std::string& sid, const Jammer& j) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->jammers.insert_or_assign(j.getId(), j);
	    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

bool SceneManager::removeJammer(const std::string& sid, int id) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return s->jammers.erase(id) > 0;
}

bool SceneManager::updateJammer(const std::string& sid, int id, const Jammer& j) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    auto it = s->jammers.find(id);
    if (it == s->jammers.end()) return false;
    it->second = j;
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

const Jammer* SceneManager::getJammer(const std::string& sid, int id) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return nullptr;
    auto it = s->jammers.find(id);
    return it != s->jammers.end() ? &it->second : nullptr;
}

std::vector<int> SceneManager::getJammerIds(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return {};
    std::vector<int> ids;
    for (const auto& [k, v] : s->jammers) ids.push_back(k);
    return ids;
}

std::vector<RadarSimResult> SceneManager::runSimulation(const std::string& sid) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s || s->radars.empty()) return {};

    SimScene scene;
    for (const auto& [id, r] : s->radars) scene.addRadar(r);
    for (const auto& [id, j] : s->jammers) scene.addJammer(j);

    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return scene.runOneStep();
}

// DQN: return raw state vector without reward calculation
std::vector<double> SceneManager::getStateForJammer(const std::string& sid, int jammer_id) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return {};

    auto jt = s->jammers.find(jammer_id);
    if (jt == s->jammers.end()) return {};

    const Jammer& jam = jt->second;
    auto [jx, jy] = jam.getPos();
    std::vector<double> state;

    for (const auto& [rid, radar] : s->radars) {
        auto [rx, ry] = radar.getPos();
        double dist = ECMAlgo::calc2DDistance(rx, ry, jx, jy);
        double delta_f = std::abs(jam.getJamFreq() - radar.getFreq());
        state.push_back(rx / 20000.0);
        state.push_back(ry / 20000.0);
        state.push_back(radar.getFreq() / 20e9);
        state.push_back(radar.getBandwidth() / 10e6);
        state.push_back(dist / 30000.0);
        state.push_back(delta_f / 10e9);
    }
    state.push_back(jam.getPjLin() / 1000.0);
    state.push_back(jam.getJamFreq() / 20e9);
    return state;
}

bool SceneManager::executeJammerAction(const std::string& sid, int jammer_id, double power_dbm, double freq) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    auto it = s->jammers.find(jammer_id);
    if (it == s->jammers.end()) return false;
    double power_lin = db2lin(power_dbm) * 1e-3;
    it->second.setJamPowerDBm(power_dbm);
    it->second.setJamFreq(freq);
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

std::string SceneManager::getSceneJson(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return "{}";

    Json::Value root;
    Json::Value radars(Json::arrayValue);
    for (const auto& [id, r] : s->radars) {
        Json::Value item;
        item["id"] = r.getId();
        item["x"] = r.getPos().first;
        item["y"] = r.getPos().second;
        item["Pt_dBm"] = lin2db(r.getPtLin() / 1e-3);
        item["G_dB"] = lin2db(r.getGLin());
        item["freq"] = r.getFreq();
        item["bandwidth"] = r.getBandwidth();
        item["sigma"] = r.getRCS();
        item["thresh_db"] = lin2db(r.getThreshLin());
        radars.append(item);
    }
    root["radars"] = radars;

    Json::Value jammers(Json::arrayValue);
    for (const auto& [id, j] : s->jammers) {
        Json::Value item;
        item["id"] = j.getId();
        item["x"] = j.getPos().first;
        item["y"] = j.getPos().second;
        item["Pj_dBm"] = lin2db(j.getPjLin() / 1e-3);
        item["Gj_dB"] = lin2db(j.getGjLin());
        item["jam_freq"] = j.getJamFreq();
        item["jam_type"] = j.getJamType() == JamType::NOISE_JAM ? "NOISE_JAM" : "RANGE_DECEPT";
        jammers.append(item);
    }
    root["jammers"] = jammers;

    Json::StyledWriter writer;
    return writer.write(root);
}

bool SceneManager::loadSceneFromJson(const std::string& sid, const std::string& jsonStr) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(jsonStr, root, false)) return false;

    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;

    s->radars.clear();
    s->jammers.clear();

    for (const auto& r : root["radars"]) {
        Radar rad(
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
        s->radars.insert_or_assign(rad.getId(), rad);
    }
    for (const auto& j : root["jammers"]) {
        JamType jt = j["jam_type"].asString() == "NOISE_JAM" ? JamType::NOISE_JAM : JamType::RANGE_DECEPT;
        Jammer jam(
            j["id"].asInt(),
            j["x"].asDouble(),
            j["y"].asDouble(),
            j["Pj_dBm"].asDouble(),
            j["Gj_dB"].asDouble(),
            j["jam_freq"].asDouble(),
            jt
        );
        s->jammers.insert_or_assign(jam.getId(), jam);
    }
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

}