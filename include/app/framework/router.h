#pragma once

#include <boost/beast/http.hpp>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace http = boost::beast::http;

struct RouteResponse {
    std::string body;
    std::string content_type = "application/json";
    http::status status = http::status::ok;
    // 额外响应头（如 Set-Cookie），在 http_session 写响应时注入
    std::vector<std::pair<std::string, std::string>> headers;
};

using RequestHandler = std::function<RouteResponse(const http::request<http::string_body>&)>;

class RouteTrie {
public:
    void addRoute(const std::string& path, RequestHandler handler);
    RouteResponse dispatch(const http::request<http::string_body>& req) const;
    void dump() const;

private:
    struct TrieNode {
        std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
        RequestHandler handler;
        bool has_handler = false;
    };

    void dumpNode(const TrieNode& node, int depth) const;

    TrieNode root_;
};
