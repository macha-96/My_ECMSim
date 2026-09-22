#pragma once

#include <boost/beast/http.hpp>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace http = boost::beast::http;

using RequestHandler = std::function<std::string(const http::request<http::string_body>&)>;

class RouteTrie {
public:
    void addRoute(const std::string& path, RequestHandler handler);
    std::string dispatch(const http::request<http::string_body>& req) const;
    void dump() const;

private:
    struct TrieNode {
        std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
        RequestHandler handler;
    };

    void dumpNode(const TrieNode& node, int depth) const;

    TrieNode root_;
};

void initRoutes(RouteTrie& trie);
