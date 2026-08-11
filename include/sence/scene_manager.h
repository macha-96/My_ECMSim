#pragma once
#include <entity/radar.h>
#include <entity/jammer.h>
#include <sence/sim_scene.h>
#include <string>
#include <map>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>

namespace ECMSim {

struct SessionData {
    std::map<int, Radar> radars;
    std::map<int, Jammer> jammers;
    uint64_t created_at = 0;
    uint64_t last_access = 0;
};

class SceneManager {
public:
    SceneManager();

    std::string createSession();
    bool deleteSession(const std::string& sid);
    bool hasSession(const std::string& sid) const;

    bool addRadar(const std::string& sid, const Radar& r);
    bool removeRadar(const std::string& sid, int id);
    bool updateRadar(const std::string& sid, int id, const Radar& r);
    const Radar* getRadar(const std::string& sid, int id) const;
    std::vector<int> getRadarIds(const std::string& sid) const;

    bool addJammer(const std::string& sid, const Jammer& j);
    bool removeJammer(const std::string& sid, int id);
    bool updateJammer(const std::string& sid, int id, const Jammer& j);
    const Jammer* getJammer(const std::string& sid, int id) const;
    std::vector<int> getJammerIds(const std::string& sid) const;

    std::vector<RadarSimResult> runSimulation(const std::string& sid);

    // DQN interfaces (raw state/action, no reward)
    std::vector<double> getStateForJammer(const std::string& sid, int jammer_id) const;
    bool executeJammerAction(const std::string& sid, int jammer_id, double power_dbm, double freq);

    // Scene serialization
    std::string getSceneJson(const std::string& sid) const;
    bool loadSceneFromJson(const std::string& sid, const std::string& jsonStr);

private:
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, std::unique_ptr<SessionData>> m_sessions;
    std::atomic<uint64_t> m_counter{0};

    SessionData* getSession(const std::string& sid);
    SessionData* getSession(const std::string& sid) const;
    std::string generateId();
};

}