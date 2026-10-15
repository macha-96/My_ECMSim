# 用户认证系统设计文档

## 1. 需求总结

| 维度 | 选择 |
|------|------|
| 目的 | 多用户场景隔离（每个用户独立管理雷达/干扰机场景） |
| 存储 | MySQL (`ecmsim_db` 数据库，docker-compose 已部署) |
| 认证 | 服务端 Session + Cookie |
| 前端 | 完整登录/注册页面 |
| 密码哈希 | bcrypt（项目已集成 `libbcrypt` 源码） |
| 数据库连接 | MySQL Connector/C++（JDBC 风格 API，项目已集成） |
| 认证范围 | 仅 HTTP API，WebSocket 暂不纳入认证 |
| 兼容策略 | 保留旧 `session_id` 机制做平滑迁移 |

## 2. 架构总览

```
┌──────────────────────────────────────────────────────────┐
│                       Frontend                             │
│                                                           │
│   login.html ──→ register.html ──→ index_v2.html          │
│   (未登录)        (未登录)         (需认证)                 │
│                                                           │
│   ┌─────────────────────────────────────────────────┐     │
│   │ 路由守卫：加载 index_v2.html 前检查              │     │
│   │ /auth/status，未认证则跳转 login.html         │     │
│   └─────────────────────────────────────────────────┘     │
└────────────────────────┬──────────────────────────────────┘
                         │ HTTP
                         ▼
┌──────────────────────────────────────────────────────────┐
│                   Beast HTTP Server                       │
│                                                           │
│  ┌──────────────────────────────────────────────────┐    │
│  │           Auth Middleware (新)                     │    │
│  │                                                    │    │
│  │  /auth/register  → 放行                        │    │
│  │  /auth/login     → 放行                        │    │
│  │  /auth/logout    → 放行                        │    │
│  │  /auth/status    → 放行                        │    │
│  │  /api/*              → 检查 Cookie session_id      │    │
│  │  /login.html         → 放行                        │    │
│  │  /register.html      → 放行                        │    │
│  │  /*                  → 需认证（除静态资源外）       │    │
│  └──────────────────────────────────────────────────┘    │
│                         │                                   │
│                         ▼                                   │
│  ┌──────────────────────────────────────────────────┐    │
│  │         AuthManager (新)                          │    │
│  │  - register(username, password)                    │    │
│  │  - login(username, password) → session_id         │    │
│  │  - logout(session_id)                             │    │
│  │  - validate(session_id) → user_id                 │    │
│  │  - getUserIdFromCookie(header) → user_id          │    │
│  └──────────────────────────────────────────────────┘    │
│                         │                                   │
│                         ▼                                   │
│  ┌──────────────────────────────────────────────────┐    │
│  │      Existing Handlers (改造)                     │    │
│  │  - 所有 /api/* handler 增加 user_id 参数          │    │
│  │  - SceneManager 操作需验证 session 归属           │    │
│  └──────────────────────────────────────────────────┘    │
└────────────────────────┬──────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────┐
│                      MySQL                                │
│  ┌──────────────┐    ┌──────────────────────┐            │
│  │ users        │    │ sessions             │            │
│  │ id           │    │ session_id (PK)      │            │
│  │ username     │◄───│ user_id (FK)         │            │
│  │ password_hash│    │ created_at           │            │
│  │ created_at   │    │ expires_at           │            │
│  └──────────────┘    └──────────────────────┘            │
└──────────────────────────────────────────────────────────┘
```

## 3. 数据库表设计

```sql
-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(32) NOT NULL UNIQUE,
    password_hash VARCHAR(60) NOT NULL COMMENT 'bcrypt hash (60 chars)',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 会话表
CREATE TABLE IF NOT EXISTS sessions (
    session_id VARCHAR(64) PRIMARY KEY COMMENT 'UUID v4',
    user_id INT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

建表使用 `CREATE TABLE IF NOT EXISTS`，幂等操作，每次启动时自动执行，无需手动迁移。

## 4. DAO 层设计

### 4.1 文件结构

```
include/app/dao/
├── mysql_pool.h      # MySQL 连接池
├── user_dao.h        # 用户表 CRUD
└── session_dao.h     # 会话表 CRUD

src/app/dao/
├── mysql_pool.cpp    # 连接池实现
├── user_dao.cpp      # 用户 DAO 实现
└── session_dao.cpp   # 会话 DAO 实现
```

### 4.2 MysqlPool — 连接池

```cpp
#pragma once
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <string>
#include <mutex>
#include <memory>
#include <queue>

namespace app::dao {

class MysqlPool {
public:
    static MysqlPool& instance();

    void initialize(const std::string& host, int port,
                    const std::string& user, const std::string& password,
                    const std::string& database, size_t pool_size = 4);

    std::unique_ptr<sql::Connection> acquire();
    void release(std::unique_ptr<sql::Connection> conn);

private:
    MysqlPool() = default;

    std::string host_, user_, password_, database_;
    int port_ = 3306;
    size_t pool_size_ = 4;

    std::mutex mtx_;
    std::queue<std::unique_ptr<sql::Connection>> pool_;
    sql::Driver* driver_ = nullptr;
};

} // namespace app::dao
```

### 4.3 UserDao — 用户表 DAO

```cpp
#pragma once
#include <string>
#include <optional>

namespace app::dao {

struct UserRecord {
    int id;
    std::string username;
    std::string password_hash;
    std::string created_at;
};

class UserDao {
public:
    // 创建用户，返回 user_id，失败返回 -1
    int createUser(const std::string& username, const std::string& password_hash);

    // 根据用户名查找用户
    std::optional<UserRecord> findByUsername(const std::string& username);

    // 根据 ID 查找用户
    std::optional<UserRecord> findById(int id);

    // 检查用户名是否已存在
    bool usernameExists(const std::string& username);
};

} // namespace app::dao
```

### 4.4 SessionDao — 会话表 DAO

```cpp
#pragma once
#include <string>
#include <optional>

namespace app::dao {

struct SessionRecord {
    std::string session_id;
    int user_id;
    std::string created_at;
    std::string expires_at;
};

class SessionDao {
public:
    // 创建会话，返回 session_id
    std::string createSession(int user_id, int ttl_hours = 24);

    // 验证会话是否有效，返回 user_id，无效返回 -1
    int validateSession(const std::string& session_id);

    // 删除会话（登出）
    bool deleteSession(const std::string& session_id);

    // 删除用户的所有会话
    bool deleteAllSessions(int user_id);

    // 清理过期会话
    void cleanupExpired();
};

} // namespace app::dao
```

## 5. AuthManager 设计

AuthManager 是业务层组件，依赖 DAO 层，封装认证逻辑：

```cpp
#pragma once
#include <string>

class AuthManager {
public:
    AuthManager();
    ~AuthManager();

    bool connect();  // 初始化 MySQL 连接池 + 建表

    // 返回空字符串表示失败
    std::string registerUser(const std::string& username, const std::string& password);
    std::string login(const std::string& username, const std::string& password);
    bool logout(const std::string& session_id);

    // 返回 user_id，-1 表示无效/过期
    int validateSession(const std::string& session_id);

    // 从 Cookie 头解析 session_id 并验证
    int getUserIdFromCookie(const std::string& cookie_header);

private:
    std::string generateUUID();
    std::string hashPassword(const std::string& password);
    bool verifyPassword(const std::string& password, const std::string& hash);
};
```

## 6. 中间件逻辑

```cpp
std::string handleRequest(const http::request<http::string_body>& req) {
    auto target = svToString(req.target());

    // 放行路由
    if (target.starts_with("/auth/") ||
        target == "/login.html" ||
        target == "/register.html" ||
        target == "/js/auth.js") {
        return routeDispatch(req);
    }

    // 检查认证
    auto cookie = req[http::field::cookie];
    int user_id = g_auth_mgr.getUserIdFromCookie(svToString(cookie));
    if (user_id < 0) {
        return jsonErr("unauthorized");  // 401
    }

    // 将 user_id 注入上下文（thread_local）
    g_thread_user_id = user_id;

    return routeDispatch(req);
}
```

## 7. Session 兼容策略

```cpp
// scene_manager.h 改造
struct SessionData {
    SimScene scene;
    uint64_t created_at = 0;
    uint64_t last_access = 0;
    int user_id = -1;  // -1 = 旧版未认证 session（向后兼容）
};

// 保留旧接口
std::string createSession() {
    return createSession(-1);  // 未认证 session
}

// 新接口
std::string createSession(int user_id) {
    // ... 原有逻辑 ...
    data->user_id = user_id;
    // ...
}
```

**迁移策略**：
- 旧 session 继续工作，`user_id = -1`
- 新登录用户的 session 携带 `user_id`
- `/api/scene/list` 等接口只返回 `user_id` 匹配的 session

## 8. API 设计

| 路由 | 方法 | 描述 |
|------|------|------|
| `/auth/register` | POST | `{username, password}` → `{success, user_id}` |
| `/auth/login` | POST | `{username, password}` → `{success, session_id}` + Set-Cookie |
| `/auth/logout` | POST | 清除 session |
| `/auth/status` | GET | 返回当前登录状态 |

## 9. 前端流程

```
用户访问 /login.html
       │
       ▼
  输入用户名/密码
       │
       ▼
  POST /auth/login
       │
       ├─ 成功 → 服务端 Set-Cookie: session_id=xxx; HttpOnly
       │         前端跳转 index_v2.html
       │
       └─ 失败 → 显示错误信息

index_v2.html 加载时：
  GET /auth/status
       │
       ├─ 已登录 → 正常加载
       └─ 未登录 → 跳转 /login.html
```

## 10. CMake 集成

### 10.1 现有依赖状态

| 组件 | CMake 文件 | 状态 |
|------|------------|------|
| bcrypt | `cmake/bcrypt.cmake` | 已集成，构建为 STATIC 库 |
| MySQL Connector/C++ | `cmake/mysql_conncpp.cmake` | 已配置，动态库 |

### 10.2 CMakeLists.txt 改动

```cmake
# 引入 bcrypt 静态库
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bcrypt.cmake)

# 引入 MySQL Connector/C++ 动态库
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/mysql_conncpp.cmake)

# 收集 DAO 源文件
file(GLOB DAO_SRCS ${PROJECT_SOURCE_DIR}/src/app/dao/*.cpp)

# 将 DAO 源文件加入可执行目标
set(APP_HTTP_BEAST_SRCS
    ${PROJECT_SOURCE_DIR}/src/app/web_server_beast.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/router.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/http_session.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/ws_session.cpp
    ${PROJECT_SOURCE_DIR}/src/app/handlers/common.cpp
    ${PROJECT_SOURCE_DIR}/src/app/handlers/routes.cpp
    ${PROJECT_SOURCE_DIR}/src/app/handlers/grpc_service.cpp
    ${PROJECT_SOURCE_DIR}/src/app/service/auth_service.cpp
    ${DAO_SRCS}
)

# 链接 bcrypt 到主可执行文件
target_link_libraries(ecmsim_http_beast
    PRIVATE
        sim_core
        bcrypt
        -Wl,--start-group
        ${GRPC_LIBS}
        ${MYSQL_CONCPP_LIB}
        ...
)
```

## 11. 依赖 API 说明

### 11.1 bcrypt API（项目已集成）

```cpp
#include <bcrypt/bcrypt.h>

// 生成 salt
char salt[BCRYPT_HASHSIZE];
bcrypt_gensalt(12, salt);  // work factor = 12

// 生成 hash
char hash[BCRYPT_HASHSIZE];
bcrypt_hashpw(password.c_str(), salt, hash);

// 验证密码
int ret = bcrypt_checkpw(password.c_str(), stored_hash.c_str());
// ret == 0 → 匹配
// ret > 0 → 不匹配
// ret == -1 → 错误
```

### 11.2 MySQL Connector/C++ API（项目已集成）

```cpp
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>

// 获取驱动实例
sql::Driver* driver = sql::mysql::get_driver_instance();

// 建立连接
std::unique_ptr<sql::Connection> conn(
    driver->connect("tcp://127.0.0.1:3306", "my_user", "my_password"));
conn->setSchema("ecmsim_db");

// PreparedStatement 防 SQL 注入
std::unique_ptr<sql::PreparedStatement> pstmt(
    conn->prepareStatement("INSERT INTO users (username, password_hash) VALUES (?, ?)"));
pstmt->setString(1, username);
pstmt->setString(2, hash);
pstmt->executeUpdate();

// 查询
std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery("SELECT ..."));
while (rs->next()) {
    int id = rs->getInt("id");
    std::string name = rs->getString("username");
}
```

## 12. 数据库初始化

在 `web_server_beast.cpp` 的 `main()` 中，启动时执行幂等建表：

```cpp
void initDatabase() {
    auto& pool = app::dao::MysqlPool::instance();
    pool.initialize("127.0.0.1", 3306, "my_user", "my_password", "ecmsim_db");

    auto conn = pool.acquire();
    auto stmt = conn->createStatement();
    stmt->execute(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(32) NOT NULL UNIQUE,
            password_hash VARCHAR(60) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )");
    stmt->execute(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            session_id VARCHAR(64) PRIMARY KEY,
            user_id INT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            expires_at TIMESTAMP NOT NULL,
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
            INDEX idx_expires (expires_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )");
    pool.release(std::move(conn));
}
```

## 13. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `CMakeLists.txt` | 修改 | 添加 cmake 包含 + DAO 源文件 + bcrypt 链接 |
| `include/app/dao/mysql_pool.h` | 新建 | MySQL 连接池声明 |
| `src/app/dao/mysql_pool.cpp` | 新建 | 连接池实现 |
| `include/app/dao/user_dao.h` | 新建 | 用户 DAO 声明 |
| `src/app/dao/user_dao.cpp` | 新建 | 用户 DAO 实现 |
| `include/app/dao/session_dao.h` | 新建 | 会话 DAO 声明 |
| `src/app/dao/session_dao.cpp` | 新建 | 会话 DAO 实现 |
| `include/app/service/auth_service.h` | 新建 | AuthManager 声明 |
| `src/app/service/auth_service.cpp` | 新建 | AuthManager 实现 |
| `static/login.html` | 新建 | 登录页面 |
| `static/register.html` | 新建 | 注册页面 |
| `static/js/auth.js` | 新建 | 前端认证逻辑 |

## 14. 实现顺序

1. **CMakeLists.txt** — 添加 `include(cmake/...)` + DAO 源文件 + bcrypt 链接
2. **mysql_pool.h/cpp** — 连接池基础设施
3. **user_dao.h/cpp** — 用户表操作
4. **session_dao.h/cpp** — 会话表操作
5. **auth.h/cpp** — AuthManager 业务逻辑
6. **数据库初始化** — `main()` 中调用建表
7. **编译验证** — `mkdir build && cd build && cmake .. && make -j$(nproc)`

## 15. 注意事项

1. **MySQL 连接信息** — 当前 docker-compose.yml 中配置为 `my_user` / `password`，后续建议改为环境变量注入
2. **session_id 生成** — 使用 UUID v4，可通过 `boost::uuids` 或手动生成
3. **Cookie 安全** — 设置 `HttpOnly` + `SameSite=Strict`，生产环境启用 `Secure`
4. **密码策略** — 注册时校验密码长度（建议最少 8 位）
5. **连接池大小** — 默认 4 个连接，可根据并发量调整
