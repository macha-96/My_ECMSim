#ifndef MY_WEB_APP_V1_H
#define MY_WEB_APP_V1_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

// 引入字典树头文件
#include "trie.h"

#define HTTP_METHOD_TABLE {"GET", "POST", "HEAD", "OPTIONS", "PUT", "PATCH", "DELETE", "TRACE", "CONNECT"}

typedef enum {GET = 0, POST, HEAD, OPTIONS, PUT, PATCH, DELETE, TRACE, CONNECT, UNKNOWN} http_method_t;

// HTTP请求上下文：解析后的请求信息 + 响应长度控制
typedef struct http_request_context {
    http_method_t method;         // 请求方法 (GET/POST/...)
    char *url;                    // 解析后的URL路径
    char *content;                // 请求体内容
    size_t url_len;
    size_t content_len;
    size_t resp_body_len;         // 业务函数设置响应体长度，供动态分配
} http_request_context_t;

// 路由服务节点：URL路径 + 业务处理函数
typedef struct service_element {
    char *url_dir;                // 路由URL key
    size_t url_dir_len;           // URL长度（加速匹配）
    void *(*service)(struct service_element*, struct http_request_context*); // 业务处理回调
} service_element_t;

// Web应用核心结构体（持有字典树句柄）
typedef struct my_web_app_v1 {
    trie_t *route_tree;
} my_web_app_v1_t;

// 对外接口声明
service_element_t *makeServiceElement(char *url_dir, size_t url_dir_len, void *(*service)(service_element_t*, http_request_context_t*));
int deleteServiceElement(service_element_t *self);
my_web_app_v1_t *makeMyWebAppV1(service_element_t **service_element_list, size_t len);
int deleteMyWebAppV1(my_web_app_v1_t *self);
int myWebAppV1MakeResponse(my_web_app_v1_t *self, char** response, char* request, int req_len);

#ifdef __cplusplus
}
#endif

#endif
