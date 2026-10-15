#pragma once
#include <jdbc/cppconn/connection.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace sql {
class Driver;
}

namespace app::dao {

// MySQL 连接池（线程安全，单例）
class MysqlPool {
public:
    static MysqlPool& instance();

    // 初始化连接池并预建连接，失败返回 false
    bool initialize(const std::string& host, int port,
                    const std::string& user, const std::string& password,
                    const std::string& database, size_t pool_size = 4);

    // 获取连接；池空时临时扩容创建，失败返回 nullptr
    std::unique_ptr<sql::Connection> acquire();

    // 归还连接；连接不健康或池已满则丢弃
    void release(std::unique_ptr<sql::Connection> conn);

    void shutdown();

private:
    MysqlPool() = default;
    std::unique_ptr<sql::Connection> createConnection();

    std::string host_, user_, password_, database_;
    int port_ = 3306;
    size_t pool_size_ = 4;
    bool initialized_ = false;

    std::mutex mtx_;
    std::queue<std::unique_ptr<sql::Connection>> pool_;
    sql::Driver* driver_ = nullptr;
};

// RAII 连接守卫：离开作用域自动归还连接池
class ConnGuard {
public:
    explicit ConnGuard(std::unique_ptr<sql::Connection> conn);
    ~ConnGuard();
    sql::Connection* operator->() const { return conn_.get(); }
    sql::Connection& operator*() const { return *conn_; }
    explicit operator bool() const { return conn_ != nullptr; }

private:
    std::unique_ptr<sql::Connection> conn_;
};

} // namespace app::dao
