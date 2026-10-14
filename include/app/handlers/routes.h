#pragma once

#include <boost/beast/http.hpp>
#include <app/framework/router.h>
#include <string>

namespace http = boost::beast::http;

void initRoutes(class RouteTrie& trie);

RouteResponse handleScene(const http::request<http::string_body>& req);
RouteResponse handleRadars(const http::request<http::string_body>& req);
RouteResponse handleRadar(const http::request<http::string_body>& req);
RouteResponse handleJammers(const http::request<http::string_body>& req);
RouteResponse handleJammer(const http::request<http::string_body>& req);
RouteResponse handleSimulate(const http::request<http::string_body>& req);
RouteResponse handleDqnState(const http::request<http::string_body>& req);
RouteResponse handleDqnAction(const http::request<http::string_body>& req);
RouteResponse handleSceneList(const http::request<http::string_body>& req);
RouteResponse handleSceneStats(const http::request<http::string_body>& req);
