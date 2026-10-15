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
    SimScene scene;
    uint64_t created_at = 0;
    uint64_t last_access = 0;
    int user_id = -1; // -1 = 旧版未认证 session（向后兼容）
};

class SceneManager {
public:
    SceneManager();

    std::string createSession();
    std::string createSession(int user_id); // 登录用户创建的场景
    bool deleteSession(const std::string& sid);
    bool hasSession(const std::string& sid) const;
    int getSessionUser(const std::string& sid) const; // 场景归属 user_id，不存在返回 -1

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

    // Resource management and statistics
    void cleanupStaleSessions(uint64_t max_age_seconds = 1800); // 30 minutes default
    std::unordered_map<std::string, uint64_t> getAllSessions() const; // sid -> last_access
    std::unordered_map<std::string, uint64_t> getSessionsByUser(int user_id) const; // 过滤归属场景
    size_t getSessionCount() const;
    size_t getTotalRadarCount() const;
    size_t getTotalJammerCount() const;

private:
    mutable std::mutex m_mtx;   // 保证对scene的操作是线程安全的
    std::unordered_map<std::string, std::unique_ptr<SessionData>> m_sessions;
    std::atomic<uint64_t> m_counter{0};

    SessionData* getSession(const std::string& sid);
    SessionData* getSession(const std::string& sid) const;
    std::string generateId();
};

}