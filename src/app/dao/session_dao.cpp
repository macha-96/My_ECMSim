#include <app/dao/session_dao.h>
#include <app/dao/mysql_pool.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <spdlog/spdlog.h>

namespace app::dao {

std::string SessionDao::generateUUID() {
    boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}

std::string SessionDao::createSession(int user_id, int ttl_hours) {
    std::string sid = generateUUID();
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return "";
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(
            "INSERT INTO sessions (session_id, user_id, expires_at) VALUES (?, ?, NOW() + INTERVAL ? HOUR)"));
        pstmt->setString(1, sid);
        pstmt->setInt(2, user_id);
        pstmt->setInt(3, ttl_hours);
        pstmt->executeUpdate();
        return sid;
    } catch (const sql::SQLException& e) {
        spdlog::error("createSession失败: {}", e.what());
        return "";
    }
}

int SessionDao::validateSession(const std::string& session_id) {
    if (session_id.empty()) return -1;
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return -1;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(
            "SELECT user_id FROM sessions WHERE session_id = ? AND expires_at > NOW()"));
        pstmt->setString(1, session_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) return -1;
        return static_cast<int>(rs->getInt64("user_id"));
    } catch (const sql::SQLException& e) {
        spdlog::error("validateSession失败: {}", e.what());
        return -1;
    }
}

bool SessionDao::deleteSession(const std::string& session_id) {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement("DELETE FROM sessions WHERE session_id = ?"));
        pstmt->setString(1, session_id);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        spdlog::error("deleteSession失败: {}", e.what());
        return false;
    }
}

bool SessionDao::deleteAllSessions(int user_id) {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement("DELETE FROM sessions WHERE user_id = ?"));
        pstmt->setInt(1, user_id);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        spdlog::error("deleteAllSessions失败: {}", e.what());
        return false;
    }
}

void SessionDao::cleanupExpired() {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return;
    try {
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        int n = stmt->executeUpdate("DELETE FROM sessions WHERE expires_at < NOW()");
        if (n > 0) spdlog::info("清理过期登录会话: {} 条", n);
    } catch (const sql::SQLException& e) {
        spdlog::error("cleanupExpired失败: {}", e.what());
    }
}

} // namespace app::dao
