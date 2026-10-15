#include <app/service/auth_service.h>
#include <app/framework/common.h>
#include <app/dao/mysql_pool.h>
#include <app/dao/user_dao.h>
#include <app/dao/session_dao.h>
#include <bcrypt/bcrypt.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include <jsoncpp/json/json.h>
#include <spdlog/spdlog.h>
#include <cstdlib>

thread_local int g_thread_user_id = -1;
AuthManager g_auth_mgr;

static RouteResponse makeResp(const std::string& body) {
    return RouteResponse{body, "application/json", http::status::ok};
}

static RouteResponse makeErr(const std::string& msg, http::status st = http::status::ok) {
    Json::Value r;
    r["success"] = false;
    r["error"] = msg;
    return RouteResponse{makeJson(r), "application/json", st};
}

// ===================== AuthManager =====================

bool AuthManager::connect() {
    const char* env_host = std::getenv("ECMSIM_DB_HOST");
    const char* env_port = std::getenv("ECMSIM_DB_PORT");
    const char* env_user = std::getenv("ECMSIM_DB_USER");
    const char* env_pass = std::getenv("ECMSIM_DB_PASSWORD");
    const char* env_db   = std::getenv("ECMSIM_DB_NAME");

    std::string host = env_host ? env_host : "127.0.0.1";
    int port = env_port ? std::atoi(env_port) : 3306;
    std::string user = env_user ? env_user : "my_user";
    std::string password = env_pass ? env_pass : "my_password";
    std::string database = env_db ? env_db : "ecmsim_db";

    if (!app::dao::MysqlPool::instance().initialize(host, port, user, password, database)) {
        return false;
    }

    // 幂等建表
    app::dao::ConnGuard conn(app::dao::MysqlPool::instance().acquire());
    if (!conn) return false;
    try {
        auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
        stmt->execute(R"(
            CREATE TABLE IF NOT EXISTS users (
                id INT AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(32) NOT NULL UNIQUE,
                password_hash VARCHAR(60) NOT NULL COMMENT 'bcrypt hash (60 chars)',
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        stmt->execute(R"(
            CREATE TABLE IF NOT EXISTS sessions (
                session_id VARCHAR(64) PRIMARY KEY COMMENT 'UUID v4',
                user_id INT NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                expires_at TIMESTAMP NOT NULL,
                FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
                INDEX idx_expires (expires_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        initialized_ = true;
        spdlog::info("认证系统已初始化（数据库: {}）", database);
        return true;
    } catch (const sql::SQLException& e) {
        spdlog::error("认证建表失败: {}", e.what());
        return false;
    }
}

std::string AuthManager::hashPassword(const std::string& password) {
    char salt[BCRYPT_HASHSIZE];
    char hash[BCRYPT_HASHSIZE];
    if (bcrypt_gensalt(12, salt) != 0) return "";
    if (bcrypt_hashpw(password.c_str(), salt, hash) != 0) return "";
    return std::string(hash);
}

bool AuthManager::verifyPassword(const std::string& password, const std::string& hash) {
    // bcrypt_checkpw: 0=匹配, >0=不匹配, -1=错误
    return bcrypt_checkpw(password.c_str(), hash.c_str()) == 0;
}

int AuthManager::registerUser(const std::string& username, const std::string& password) {
    if (!initialized_ || username.empty() || password.size() < 8) return -1;
    if (username.size() > 32) return -1;

    app::dao::UserDao user_dao;
    if (user_dao.usernameExists(username)) return -1;

    std::string hash = hashPassword(password);
    if (hash.empty()) return -1;

    return user_dao.createUser(username, hash);
}

std::string AuthManager::login(const std::string& username, const std::string& password) {
    if (!initialized_) return "";

    app::dao::UserDao user_dao;
    auto user = user_dao.findByUsername(username);
    if (!user) return "";

    if (!verifyPassword(password, user->password_hash)) return "";

    app::dao::SessionDao session_dao;
    return session_dao.createSession(user->id);
}

bool AuthManager::logout(const std::string& session_id) {
    if (!initialized_ || session_id.empty()) return false;
    app::dao::SessionDao session_dao;
    return session_dao.deleteSession(session_id);
}

int AuthManager::validateSession(const std::string& session_id) {
    if (!initialized_ || session_id.empty()) return -1;
    app::dao::SessionDao session_dao;
    return session_dao.validateSession(session_id);
}

int AuthManager::getUserIdFromCookie(const std::string& cookie_header) {
    // 解析 "a=1; session_id=xxx; b=2" 中的 session_id
    size_t pos = cookie_header.find("session_id=");
    if (pos == std::string::npos) return -1;
    pos += 11; // strlen("session_id=")
    if (pos >= cookie_header.size()) return -1;
    size_t end = cookie_header.find(';', pos);
    std::string sid = (end == std::string::npos)
                          ? cookie_header.substr(pos)
                          : cookie_header.substr(pos, end - pos);
    return validateSession(sid);
}

bool AuthManager::usernameExists(const std::string& username) {
    if (!initialized_) return false;
    app::dao::UserDao user_dao;
    return user_dao.usernameExists(username);
}

void AuthManager::cleanupExpiredSessions() {
    if (!initialized_) return;
    app::dao::SessionDao session_dao;
    session_dao.cleanupExpired();
}

// ===================== HTTP handlers =====================

RouteResponse handleAuthRegister(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) return makeErr("use POST");

    auto body = parseJson(svToString(req.body()));
    std::string username = body["username"].asString();
    std::string password = body["password"].asString();

    if (username.empty() || username.size() > 32) return makeErr("username must be 1-32 chars");
    if (password.size() < 8) return makeErr("password must be at least 8 chars");
    if (g_auth_mgr.usernameExists(username)) return makeErr("username already exists");

    int uid = g_auth_mgr.registerUser(username, password);
    if (uid < 0) return makeErr("register failed");

    Json::Value r;
    r["success"] = true;
    r["user_id"] = uid;
    spdlog::info("用户注册成功: {} (id={})", username, uid);
    return makeResp(makeJson(r));
}

RouteResponse handleAuthLogin(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) return makeErr("use POST");

    auto body = parseJson(svToString(req.body()));
    std::string username = body["username"].asString();
    std::string password = body["password"].asString();

    if (username.empty() || password.empty()) return makeErr("need username and password");

    std::string sid = g_auth_mgr.login(username, password);
    if (sid.empty()) return makeErr("invalid username or password");

    Json::Value r;
    r["success"] = true;
    r["session_id"] = sid;
    RouteResponse resp = makeResp(makeJson(r));
    // HttpOnly: JS 不可读；SameSite=Strict 防 CSRF
    resp.headers.push_back(
        {"Set-Cookie", "session_id=" + sid + "; HttpOnly; Path=/; SameSite=Strict"});
    spdlog::info("用户登录成功: {} (session={})", username, sid);
    return resp;
}

RouteResponse handleAuthLogout(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::post) return makeErr("use POST");

    std::string cookie = svToString(req[http::field::cookie]);
    int uid = g_auth_mgr.getUserIdFromCookie(cookie);
    if (uid >= 0) {
        // 提取 session_id 值（截断到下一个 ';'）
        size_t pos = cookie.find("session_id=");
        if (pos != std::string::npos) {
            pos += 11;
            size_t end = cookie.find(';', pos);
            std::string sid = (end == std::string::npos)
                                  ? cookie.substr(pos)
                                  : cookie.substr(pos, end - pos);
            g_auth_mgr.logout(sid);
        }
    }

    Json::Value r;
    r["success"] = true;
    RouteResponse resp = makeResp(makeJson(r));
    resp.headers.push_back({"Set-Cookie", "session_id=; HttpOnly; Path=/; Max-Age=0"});
    return resp;
}

RouteResponse handleAuthStatus(const http::request<http::string_body>& req) {
    if (req.method() != http::verb::get) return makeErr("use GET");

    std::string cookie = svToString(req[http::field::cookie]);
    int uid = g_auth_mgr.getUserIdFromCookie(cookie);

    Json::Value r;
    r["success"] = true;
    r["authenticated"] = uid >= 0;
    if (uid >= 0) r["user_id"] = uid;
    return makeResp(makeJson(r));
}

// ===================== 认证中间件 =====================

RequestHandler requireAuth(RequestHandler handler) {
    return [handler](const http::request<http::string_body>& req) -> RouteResponse {
        std::string cookie = svToString(req[http::field::cookie]);
        int uid = g_auth_mgr.getUserIdFromCookie(cookie);
        if (uid < 0) {
            return makeErr("unauthorized", http::status::unauthorized);
        }
        g_thread_user_id = uid;
        RouteResponse resp = handler(req);
        g_thread_user_id = -1;
        return resp;
    };
}
