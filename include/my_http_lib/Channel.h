#ifndef MY_CHANNEL_H
#define MY_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/epoll.h>
#include <Socket.h>
#include <stdatomic.h>
#include "EventLoop.h"
// #include "Epoll.h"

#define myChannelSetThreadPool(self, thread_pool_instance) ((self)->ep_->thread_pool_ = (thread_pool_instance))

// 前置声明 epoll 结构体，代替 #include "Epoll.h"
struct my_epoll;
typedef struct my_epoll my_epoll_t;
// 最后需要在Channel.c文件中 #include <Epoll.h>

// typedef enum {LISTEN = 0, CLIENT} my_socket_mode_t;

typedef struct my_channel {
    int fd_;                        // channel 和 fd 是一一对应的关系，一个channel只拥有一个fd
    my_epoll_t *ep_;                // channel 对应的 epoll对象(在内核中)，一个epoll对象可以对应多个channel，但是一个channel对象只对应一个epoll对象
    bool in_epoll_;                 // 标志位 channel 是否已经注册到 epoll 对象中，如果没有添加，需要在调用epoll_ctl()的时候用EPOLL_CTL_ADD(添加)，否则用EPOLL_CTL_MOD(修改)
    uint32_t events_;               // fd_需要监听的事件。listenfd和clientfd需要监视EPOLLIN，clientfd还可能需要监视EPOLLOUT.
    uint32_t revents_;              // fd_已经发生的事件
    // my_socket_mode_t sock_mod_;
    
    void (*rd_event_callback_)(struct my_channel*, void*);      // 读事件回调函数
    void *rd_cb_args_;

    void (*wr_event_callback_)(struct my_channel*, void*);      // 写事件回调函数
    void *wr_cb_args_;
#ifdef USE_THREAD_POOL
    // bool closing;                   // 是否开始关闭流程
    atomic_int pending_task_;       // 引用计数，记录这个channel对象被几个线程引用，设置为原子变量保证对它的操作是线程安全的
#endif
} my_channel_t;

struct default_cli_rdcb_args {
    size_t rdy_list_idx;
};

my_channel_t *makeMyChannel(my_epoll_t *ep, int fd);                        // 构造函数
int myChannelGetFd(my_channel_t *self);                                     // 获取fd_成员
void myChannelUselt(my_channel_t *self);                                    // 采用水平触发
void myChannelUseet(my_channel_t *self);                                    // 采用边缘触发
void myChannelReadingCmd(my_channel_t *self, my_status_t cmd);              // 让epoll_wait()监视fd_的读事件(使能或失能)
void myChannelSetInEpoll(my_channel_t *self, my_status_t cmd);              // 设置in_epoll_成员的值
void myChannelSetRevents(my_channel_t *self, uint32_t ev);                  // 设置revents_的成员为ev
bool myChannelGetInEpoll(my_channel_t *self);                               // 获取in_epoll_成员的值
uint32_t myChannelGetEvents(my_channel_t *self);                            // 返回events_成员
uint32_t myChannelGetRevents(my_channel_t *self);                           // 返回revents_成员
int myChannelHandleEvent(my_channel_t *self);         // 事件处理函数，epoll_wait返回的时候执行这个函数
int myChannelSetReadEventCallback(my_channel_t *self, void (*cb)(struct my_channel*, void*), void *args, size_t len);      // 设置读事件回调函数
int myChannelSetWriteEventCallback(my_channel_t *self, void (*cb)(struct my_channel*, void*), void *args, size_t len);     // 设置写事件回调函数
void deleteMyChannel(my_channel_t *self);                                   // 析构函数

extern void defaultListenReadCallback(my_channel_t *self, void *args);      // 默认给listen_sock 的读事件触發的回调函数
extern void defaultClientReadCallback(my_channel_t *self, void *args);      // 默认给client_sock 的读事件触發的回调函数

#ifdef __cplusplus
}
#endif

#endif