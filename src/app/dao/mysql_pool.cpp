#include <app/dao/mysql_pool.h>
#include <mysql_driver.h>
#include <jdbc/cppconn/exception.h>
#include <spdlog/spdlog.h>

namespace app::dao {

MysqlPool& MysqlPool::instance() {
    static MysqlPool pool;
    return pool;
}

std::unique_ptr<sql::Connection> MysqlPool::createConnection() {
    try {
        std::string url = "tcp://" + host_ + ":" + std::to_string(port_);
        std::unique_ptr<sql::Connection> conn(driver_->connect(url, user_, password_));
        conn->setSchema(database_);
        return conn;
    } catch (const sql::SQLException& e) {
        spdlog::error("MySQL连接失败 {}:{} - {}", host_, port_, e.what());
        return nullptr;
    }
}

bool MysqlPool::initialize(const std::string& host, int port,
                           const std::string& user, const std::string& password,
                           const std::string& database, size_t pool_size) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (initialized_) return true;

    host_ = host;
    port_ = port;
    user_ = user;
    password_ = password;
    database_ = database;
    pool_size_ = pool_size;

    try {
        driver_ = sql::mysql::get_driver_instance();
    } catch (const sql::SQLException& e) {
        spdlog::error("获取MySQL驱动失败: {}", e.what());
        return false;
    }

    for (size_t i = 0; i < pool_size_; ++i) {
        auto conn = createConnection();
        if (!conn) return false;
        pool_.push(std::move(conn));
    }

    initialized_ = true;
    spdlog::info("MySQL连接池已初始化: {}:{}/{} ({}个连接)", host_, port_, database_, pool_size_);
    return true;
}

std::unique_ptr<sql::Connection> MysqlPool::acquire() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return nullptr;

    if (pool_.empty()) {
        // 池空时临时扩容创建新连接
        return createConnection();
    }

    auto conn = std::move(pool_.front());
    pool_.pop();

    // 校验连接有效性，失效则重建
    try {
        if (!conn || !conn->isValid()) {
            conn = createConnection();
        }
    } catch (const sql::SQLException&) {
        conn = createConnection();
    }
    return conn;
}

void MysqlPool::release(std::unique_ptr<sql::Connection> conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mtx_);
    bool healthy = false;
    try {
        healthy = !conn->isClosed() && conn->isValid();
    } catch (...) {
        healthy = false;
    }
    if (healthy && pool_.size() < pool_size_) {
        pool_.push(std::move(conn));
    }
    // 不健康或池已满：丢弃，unique_ptr 析构自动关闭连接
}

void MysqlPool::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!pool_.empty()) pool_.pop();
    initialized_ = false;
}

ConnGuard::ConnGuard(std::unique_ptr<sql::Connection> conn) : conn_(std::move(conn)) {}

ConnGuard::~ConnGuard() {
    if (conn_) MysqlPool::instance().release(std::move(conn_));
}

} // namespace app::dao
