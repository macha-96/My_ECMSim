#ifndef MY_EPOLL_H
#define MY_EPOLL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/epoll.h>
#include <stdint.h>
#include "Socket.h"
#ifdef USE_THREAD_POOL
#include "MyThreadPool.h"
#include "MyDeque.h"
#endif
// #include "Channel.h"

// 前置声明，代替 #include "Channel.h"，解决循环依赖
struct my_channel;
typedef struct my_channel my_channel_t;
// 最后需要放到Epoll.c文件中 #include <Channel.h>

#define READY_CH(my_epoll_obj,index) ((my_channel_t *)((my_epoll_obj)->ready_list_[(index)].data.ptr))
#define READY_FD(my_epoll_obj,index) (READY_CH(my_epoll_obj,index)->fd_)

#define READY_EV(my_epoll_obj,index) ((my_epoll_obj)->ready_list_[(index)].events)

typedef struct my_epoll {
    int epollfd_;                           // epoll 的文件描述符
    size_t ready_list_size_;                // 就绪列表的大小
    size_t ready_list_capacity_;            // 就绪列表的容量
    struct epoll_event *ready_list_;        // 就绪列表的指针

#ifdef USE_THREAD_POOL
    my_thread_pool_t *thread_pool_;         // 处理事件的线程池
    my_deque_t *msg_queue_;
    pthread_mutex_t mtx_;
    // pthread_cond_t cv_;
#endif
} my_epoll_t;

my_epoll_t *makeMyEpoll(size_t ready_list_size);
int myEpollRegistChannel(my_epoll_t *self, my_channel_t *ch);
int myEpollUnRegistChannel(my_epoll_t *self, my_channel_t *ch);
int myEpollWait(my_epoll_t *self, int timeout);
void deleteMyEpoll(my_epoll_t *self);

#ifdef __cplusplus
}
#endif

#endif