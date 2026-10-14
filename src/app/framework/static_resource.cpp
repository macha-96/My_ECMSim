#include <app/framework/static_resource.h>
#include <app/framework/common.h>

RouteResponse serveStaticFile(const std::string& target) {
    if (!isPathSafe(target)) {
        return RouteResponse{
            "{\"success\":false,\"error\":\"invalid path\"}",
            "application/json",
            http::status::forbidden
        };
    }

    std::string path = target;
    if (path == "/") path = "/index.html";

    auto it = g_static_cache.find(path);
    if (it != g_static_cache.end()) {
        return RouteResponse{it->second, getMimeType(path), http::status::ok};
    }

    return RouteResponse{
        "{\"success\":false,\"error\":\"file not found: " + path + "\"}",
        "application/json",
        http::status::not_found
    };
}