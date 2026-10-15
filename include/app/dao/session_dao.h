#pragma once
#include <string>

namespace app::dao {

// sessions 表数据访问（登录会话，区别于 SceneManager 的场景会话）
class SessionDao {
public:
    // 创建登录会话，返回 session_id（UUID v4），失败返回空串
    std::string createSession(int user_id, int ttl_hours = 24);

    // 验证会话是否有效，返回 user_id；无效/过期返回 -1
    int validateSession(const std::string& session_id);

    // 删除会话（登出），成功返回 true
    bool deleteSession(const std::string& session_id);

    // 删除用户的所有会话
    bool deleteAllSessions(int user_id);

    // 清理过期会话
    void cleanupExpired();

private:
    static std::string generateUUID();
};

} // namespace app::dao
