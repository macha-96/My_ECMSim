#pragma once
#include <optional>
#include <string>

namespace app::dao {

struct UserRecord {
    int id = -1;
    std::string username;
    std::string password_hash;
    std::string created_at;
};

// users 表数据访问
class UserDao {
public:
    // 创建用户，返回自增 user_id，失败返回 -1
    int createUser(const std::string& username, const std::string& password_hash);

    // 根据用户名查找用户
    std::optional<UserRecord> findByUsername(const std::string& username);

    // 根据 ID 查找用户
    std::optional<UserRecord> findById(int id);

    // 用户名是否已存在
    bool usernameExists(const std::string& username);
};

} // namespace app::dao
