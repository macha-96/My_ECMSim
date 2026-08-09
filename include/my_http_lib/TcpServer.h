#ifndef TCPSERVER_H
#define TCPSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "EventLoop.h"
#include "my_web_app_v1.h"

#define myTcpServerSetReadCbApp(self, app) ((self)->cli_sk_read_cb_args_->app_ = (app))
#define myTcpServerSetReadCbMkResp(self, mkResp) ((self)->cli_sk_read_cb_args_->makeResponse_ = (mkResp))
#define myTcpServerSetCliSkReadCbArgs(self, args) ((self)->cli_sk_read_cb_args_ = (args))

typedef struct cli_sk_read_cb_args {
    int (*makeResponse_)(my_web_app_v1_t*, char**, char*, int);
    my_web_app_v1_t *app_;
}cli_sk_read_cb_args_t;

typedef struct my_tcp_server {
    // 一个 TCP server可以有多个事件循环，现在是单线程，所以只有一个事件循环
    bool is_running_;
    event_loop_t *ev_loop_;
    my_channel_t *serv_ch_;
    
    cli_sk_read_cb_args_t *cli_sk_read_cb_args_;
}my_tcp_server_t;

cli_sk_read_cb_args_t *makeCliSkReadCbArgs(int (*)(my_web_app_v1_t*, char**, char*, int), my_web_app_v1_t*);
my_tcp_server_t *makeMyTcpServer(char *ip_addr, int port_no);
void myTcpServerStart(my_tcp_server_t *self);
void deleteMyTcpServer(my_tcp_server_t *self);

#ifdef __cplusplus
}
#endif

#endif