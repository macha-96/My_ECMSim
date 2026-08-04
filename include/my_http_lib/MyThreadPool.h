#ifndef MY_THREAD_POOL
#define MY_THREAD_POOL

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct task_queue_node {
    void *(*task)(void*);
    void *args;
    size_t priority;
    struct task_queue_node* prior;
    struct task_queue_node* next;
}task_queue_node_t;

typedef struct my_thread_pool {
    task_queue_node_t* task_queue_head;
    size_t task_queue_size;

    pthread_mutex_t taskq_mtx;
    pthread_cond_t taskq_cond;
    
    pthread_t *thread_list;
    size_t thread_num;

    bool shutdown;
}my_thread_pool_t;

my_thread_pool_t *createThreadPool(size_t thread_num);
int enqueueTask(my_thread_pool_t* self, size_t priority, void *(*task)(void*), void* args);
int deleteThreadPool(my_thread_pool_t* self);

#ifdef __cplusplus
}
#endif

#endif