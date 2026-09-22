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

#include <app/common.h>
#include <app/http_session.h>
#include <app/grpc_service.h>
#include <app/router.h>
#include <spdlog/spdlog.h>

RouteTrie g_router;

int main(int argc, char* argv[]) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    spdlog::set_level(spdlog::level::info);

    // 加载前端界面
    const char* staticDir = argc >= 2 ? argv[1] : "static";
    std::string indexPath = std::string(staticDir) + "/index_v2.html";
    if (!loadFile(indexPath, g_index_html)) {
        spdlog::error("无法加载前端页面: {}", indexPath);
        return 1;
    }
    spdlog::info("加载前端页面: {} ({} bytes)", indexPath, g_index_html.size());

    // 初始化路由标
    initRoutes(g_router);
    spdlog::info("路由表已初始化");
    g_router.dump();

    constexpr int num_threads = 4;          // 4 线程的IO事件循环
    net::io_context ioc{num_threads};

    // 启动web服务器
    tcp::endpoint endpoint{net::ip::make_address("0.0.0.0"), 8080};
    http_server server(ioc, endpoint);
    spdlog::info("HTTP服务器已启动, 端口: 8080");

    // 启动gRPC服务器
    AgentSvc agentSvc;
    grpc::ServerBuilder gb;
    gb.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
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
