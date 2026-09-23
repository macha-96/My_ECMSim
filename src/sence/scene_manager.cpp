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
    s->scene.addRadar(r);
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

bool SceneManager::removeRadar(const std::string& sid, int id) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return s->scene.removeRadar(id);
}

bool SceneManager::updateRadar(const std::string& sid, int id, const Radar& r) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    bool ok = s->scene.updateRadar(id, r);
    if (ok) s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return ok;
}

const Radar* SceneManager::getRadar(const std::string& sid, int id) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return nullptr;
    return s->scene.getRadar(id);
}

std::vector<int> SceneManager::getRadarIds(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return {};
    return s->scene.getRadarIds();
}

bool SceneManager::addJammer(const std::string& sid, const Jammer& j) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->scene.addJammer(j);
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

bool SceneManager::removeJammer(const std::string& sid, int id) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return s->scene.removeJammer(id);
}

bool SceneManager::updateJammer(const std::string& sid, int id, const Jammer& j) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    bool ok = s->scene.updateJammer(id, j);
    if (ok) s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return ok;
}

const Jammer* SceneManager::getJammer(const std::string& sid, int id) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return nullptr;
    return s->scene.getJammer(id);
}

std::vector<int> SceneManager::getJammerIds(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return {};
    return s->scene.getJammerIds();
}

std::vector<RadarSimResult> SceneManager::runSimulation(const std::string& sid) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s || !s->scene.hasRadars()) return {};
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return s->scene.runOneStep();
}

std::vector<double> SceneManager::getStateForJammer(const std::string& sid, int jammer_id) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return {};
    return s->scene.getStateForJammer(jammer_id);
}

bool SceneManager::executeJammerAction(const std::string& sid, int jammer_id, double power_dbm, double freq) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return false;
    if (!s->scene.getJammer(jammer_id)) return false;
    s->scene.executeJammerAction(jammer_id, power_dbm, freq);
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

std::string SceneManager::getSceneJson(const std::string& sid) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto* s = getSession(sid);
    if (!s) return "{}";

    Json::Value root;
    Json::Value radars(Json::arrayValue);
    for (const auto& [id, r] : s->scene.getRadars()) {
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
    for (const auto& [id, j] : s->scene.getJammers()) {
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

    s->scene.clearAll();

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
        s->scene.addRadar(rad);
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
        s->scene.addJammer(jam);
    }
    s->last_access = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

void SceneManager::cleanupStaleSessions(uint64_t max_age_seconds) {
    std::lock_guard<std::mutex> lock(m_mtx);
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    uint64_t cutoff = now - max_age_seconds;

    auto it = m_sessions.begin();
    while (it != m_sessions.end()) {
        if (it->second->last_access < cutoff) {
            // 先通知WebSocket广播器这个会话被清理
            // 注意：这里不能直接调用g_broadcaster，因为它在不同的编译单元
            // WebSocket清理会在web_server_beast.cpp的cleanup中处理
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

std::unordered_map<std::string, uint64_t> SceneManager::getAllSessions() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    std::unordered_map<std::string, uint64_t> result;
    for (const auto& [sid, data] : m_sessions) {
        result[sid] = data->last_access;
    }
    return result;
}

size_t SceneManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_sessions.size();
}

size_t SceneManager::getTotalRadarCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    size_t total = 0;
    for (const auto& [sid, data] : m_sessions) {
        total += data->scene.getRadars().size();
    }
    return total;
}

size_t SceneManager::getTotalJammerCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    size_t total = 0;
    for (const auto& [sid, data] : m_sessions) {
        total += data->scene.getJammers().size();
    }
    return total;
}

}
