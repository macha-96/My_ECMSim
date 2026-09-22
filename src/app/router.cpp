#include <app/router.h>
#include <app/http_handlers.h>
#include <app/common.h>
#include <spdlog/spdlog.h>
#include <sstream>

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segments;
    std::string seg;
    for (char c : path) {
        if (c == '/') {
            if (!seg.empty()) {
                segments.push_back(seg);
                seg.clear();
            }
        } else {
            seg += c;
        }
    }
    if (!seg.empty()) {
        segments.push_back(seg);
    }
    return segments;
}

void RouteTrie::addRoute(const std::string& path, RequestHandler handler) {
    auto segments = splitPath(path);
    TrieNode* cur = &root_;
    for (const auto& seg : segments) {
        if (cur->children.find(seg) == cur->children.end()) {
            cur->children[seg] = std::make_unique<TrieNode>();
        }
        cur = cur->children[seg].get();
    }
    cur->handler = std::move(handler);
    spdlog::debug("注册路由: {}", path);
}

std::string RouteTrie::dispatch(const http::request<http::string_body>& req) const {
    std::string target = std::string(req.target());
    size_t qpos = target.find('?');
    if (qpos != std::string::npos) {
        target = target.substr(0, qpos);
    }

    auto segments = splitPath(target);
    const TrieNode* cur = &root_;
    for (const auto& seg : segments) {
        auto it = cur->children.find(seg);
        if (it == cur->children.end()) {
            return "{\"success\":false,\"error\":\"unknown route: " + target + "\"}";
        }
        cur = it->second.get();
    }

    if (!cur->handler) {
        return "{\"success\":false,\"error\":\"no handler: " + target + "\"}";
    }

    return cur->handler(req);
}

void RouteTrie::dumpNode(const TrieNode& node, int depth) const {
    for (const auto& [seg, child] : node.children) {
        std::string indent(depth * 2, ' ');
        spdlog::info("{}└── {}", indent, seg);
        dumpNode(*child, depth + 1);
    }
}

void RouteTrie::dump() const {
    spdlog::info("路由字典树:");
    dumpNode(root_, 0);
}

void initRoutes(RouteTrie& trie) {
    trie.addRoute("/", [](const http::request<http::string_body>& req) -> std::string {
        return g_index_html;
    });

    trie.addRoute("/api/scene", handleScene);
    trie.addRoute("/api/scene/list", handleSceneList);
    trie.addRoute("/api/scene/stats", handleSceneStats);
    trie.addRoute("/api/radars", handleRadars);
    trie.addRoute("/api/radar", handleRadar);
    trie.addRoute("/api/jammers", handleJammers);
    trie.addRoute("/api/jammer", handleJammer);
    trie.addRoute("/api/simulate", handleSimulate);
    trie.addRoute("/api/dqn/state", handleDqnState);
    trie.addRoute("/api/dqn/action", handleDqnAction);
}
