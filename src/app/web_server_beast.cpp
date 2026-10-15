/**
 * @file web_server_beast.cpp
 * @brief ECMSim V3 主服务器入口 (HTTP + gRPC + WebSocket)
 *
 * 本文件仅包含 main() 函数和服务器启动逻辑。
 * 各模块拆分到独立文件:
 *   - common.h/.cpp         — 工具函数、全局状态、cleanup_controller
 *   - ws_session.h/.cpp     — WebSocket 会话管理、广播器
 *   - http_handlers.h/.cpp  — HTTP API 处理函数
 *   - http_session.h/.cpp   — HTTP 会话类、HTTP 服务器类
 *   - grpc_service.h/.cpp   — gRPC AgentService 实现
 *   - router.h/.cpp         — 字典树路由分发
 */


#include <app/framework/http_session.h>
#include <app/service/grpc_service.h>
#include <app/service/auth_service.h>
#include <app/framework/router.h>
#include <app/framework/common.h>
#include <app/service/web_service.h>
#include <spdlog/spdlog.h>
#include <util/cmdline.h>

RouteTrie g_router;
cmdline::parser argv_parser;

static void prase_args(int argc, char* argv[], std::string &host, uint16_t &port, std::string &static_dir) {
    // 加入指定类型的输入參数
    // 第一个參数：长名称
    // 第二个參数：短名称（'\0'表示没有短名称）
    // 第三个參数：參数描写叙述
    // 第四个參数：bool值，表示该參数是否必须存在（可选。默认值是false）
    // 第五个參数：參数的默认值（可选，当第四个參数为false时该參数有效）
    argv_parser.add<std::string>("host", 'h', "host name", false, "0.0.0.0");
 
    // 第六个參数用来对參数加入额外的限制
    // 这里端口号被限制为必须是1到65535区间的值，通过cmdline::range(1, 65535)进行限制 
    argv_parser.add<uint16_t>("port", 'p', "port number", false, 8080, cmdline::range(1, 65535));

    argv_parser.add<std::string>("static-dir", 's', "static resource directory", false , "static");
    
    argv_parser.parse_check(argc, argv);

    host = argv_parser.get<std::string>("host");
    port = argv_parser.get<uint16_t>("port");
    static_dir = argv_parser.get<std::string>("static-dir");
}

int main(int argc, char* argv[]) {
    // 初始化日志
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    spdlog::set_level(spdlog::level::info);

    // 加载命令行参数
    std::string host, static_dir;
    uint16_t port;
    prase_args(argc, argv, host, port, static_dir);

    // 加载静态文件目录
    if (!loadStaticDir(static_dir)) {
        spdlog::error("无法加载静态文件目录: {}", static_dir);
        return 1;
    }

    // 初始化认证系统（MySQL 连接池 + 幂等建表），失败则快速退出
    if (!g_auth_mgr.connect()) {
        spdlog::critical("认证系统初始化失败：无法连接 MySQL ({}:{})",
                         std::getenv("ECMSIM_DB_HOST") ? std::getenv("ECMSIM_DB_HOST") : "127.0.0.1",
                         std::getenv("ECMSIM_DB_PORT") ? std::getenv("ECMSIM_DB_PORT") : "3306");
        return 1;
    }

    // 初始化路由表
    initRoutes(g_router);
    spdlog::info("路由表已初始化");
    g_router.dump();

    // 创建io_context
    constexpr int num_threads = 4;          // 4 线程的IO事件循环
    net::io_context ioc{num_threads};

    // 启动web服务器
    tcp::endpoint endpoint{net::ip::make_address(host), port};
    http_server server(ioc, endpoint);
    spdlog::info("HTTP服务器已启动, 监听地址：{}, 端口: {}", host, port);
    spdlog::info("回环地址: http://localhost:{}", port);

    // 启动gRPC服务器
    AgentSvc agentSvc;
    grpc::ServerBuilder gb;
    gb.AddListeningPort(host + ":" + "50051", grpc::InsecureServerCredentials());
    gb.RegisterService(&agentSvc);
    auto gs = gb.BuildAndStart();
    if (!gs) {
        spdlog::error("gRPC服务器启动失败");
        return 1;
    }
    spdlog::info("gRPC服务器已启动, 端口: 50051");

    // 启动资源清理进程
    cleanup_controller cleanup;
    cleanup.start();

    std::vector<std::thread> threads;
    threads.reserve(num_threads - 1);
    for (int i = 0; i < num_threads - 1; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }
    gs->Wait();
    ioc.run();
    for (auto& t : threads) t.join();

    return 0;
}
