#pragma once

#include <boost/beast/http.hpp>
#include <string>

namespace http = boost::beast::http;

std::string handleScene(const http::request<http::string_body>& req);
std::string handleRadars(const http::request<http::string_body>& req);
std::string handleRadar(const http::request<http::string_body>& req);
std::string handleJammers(const http::request<http::string_body>& req);
std::string handleJammer(const http::request<http::string_body>& req);
std::string handleSimulate(const http::request<http::string_body>& req);
std::string handleDqnState(const http::request<http::string_body>& req);
std::string handleDqnAction(const http::request<http::string_body>& req);
std::string handleSceneList(const http::request<http::string_body>& req);
std::string handleSceneStats(const http::request<http::string_body>& req);
