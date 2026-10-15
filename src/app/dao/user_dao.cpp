#include <app/dao/user_dao.h>
#include <app/dao/mysql_pool.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include <spdlog/spdlog.h>

namespace app::dao {

int UserDao::createUser(const std::string& username, const std::string& password_hash) {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return -1;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement("INSERT INTO users (username, password_hash) VALUES (?, ?)"));
        pstmt->setString(1, username);
        pstmt->setString(2, password_hash);
        pstmt->executeUpdate();

        // 取自增主键
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
        int id = -1;
        if (rs->next()) id = static_cast<int>(rs->getInt64(1));
        return id;
    } catch (const sql::SQLException& e) {
        spdlog::error("createUser失败: {}", e.what());
        return -1;
    }
}

std::optional<UserRecord> UserDao::findByUsername(const std::string& username) {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return std::nullopt;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(
            "SELECT id, username, password_hash, created_at FROM users WHERE username = ?"));
        pstmt->setString(1, username);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) return std::nullopt;

        UserRecord rec;
        rec.id = static_cast<int>(rs->getInt64("id"));
        rec.username = rs->getString("username");
        rec.password_hash = rs->getString("password_hash");
        rec.created_at = rs->getString("created_at");
        return rec;
    } catch (const sql::SQLException& e) {
        spdlog::error("findByUsername失败: {}", e.what());
        return std::nullopt;
    }
}

std::optional<UserRecord> UserDao::findById(int id) {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return std::nullopt;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(
            "SELECT id, username, password_hash, created_at FROM users WHERE id = ?"));
        pstmt->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) return std::nullopt;

        UserRecord rec;
        rec.id = static_cast<int>(rs->getInt64("id"));
        rec.username = rs->getString("username");
        rec.password_hash = rs->getString("password_hash");
        rec.created_at = rs->getString("created_at");
        return rec;
    } catch (const sql::SQLException& e) {
        spdlog::error("findById失败: {}", e.what());
        return std::nullopt;
    }
}

bool UserDao::usernameExists(const std::string& username) {
    ConnGuard conn(MysqlPool::instance().acquire());
    if (!conn) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement("SELECT 1 FROM users WHERE username = ?"));
        pstmt->setString(1, username);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        return rs->next();
    } catch (const sql::SQLException& e) {
        spdlog::error("usernameExists失败: {}", e.what());
        return false;
    }
}

} // namespace app::dao
