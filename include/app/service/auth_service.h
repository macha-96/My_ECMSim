#pragma once

#include <app/framework/router.h>
#include <boost/beast/http.hpp>
#include <string>

namespace http = boost::beast::http;

// 当前请求已认证的用户 ID（由 requireAuth 中间件注入，-1 = 未认证）
extern thread_local int g_thread_user_id;

// 认证管理器：用户注册/登录/登出/会话验证（业务层，依赖 DAO 层）
class AuthManager {
public:
    // 初始化 MySQL 连接池 + 幂等建表，失败返回 false
    bool connect();

    // 注册：成功返回 user_id(>=0)，失败返回 -1
    int registerUser(const std::string& username, const std::string& password);

    // 登录：成功返回 session_id，失败返回空串
    std::string login(const std::string& username, const std::string& password);

    // 登出：删除登录会话
    bool logout(const std::string& session_id);

    // 验证会话：有效返回 user_id，无效/过期返回 -1
    int validateSession(const std::string& session_id);

    // 从 Cookie 头解析 session_id 并验证，返回 user_id（-1 无效）
    int getUserIdFromCookie(const std::string& cookie_header);

    // 用户名是否已存在
    bool usernameExists(const std::string& username);

    // 清理过期登录会话
    void cleanupExpiredSessions();

private:
    std::string hashPassword(const std::string& password);
    bool verifyPassword(const std::string& password, const std::string& hash);

    bool initialized_ = false;
};

extern AuthManager g_auth_mgr;

// /auth/* HTTP handler（无需登录）
RouteResponse handleAuthRegister(const http::request<http::string_body>& req);
RouteResponse handleAuthLogin(const http::request<http::string_body>& req);
RouteResponse handleAuthLogout(const http::request<http::string_body>& req);
RouteResponse handleAuthStatus(const http::request<http::string_body>& req);

// 认证中间件：包装 handler，校验 Cookie 中的 session_id。
// 未认证返回 401；认证通过后注入 g_thread_user_id 再执行原 handler。
RequestHandler requireAuth(RequestHandler handler);
