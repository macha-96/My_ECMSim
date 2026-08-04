#ifndef DEQUE_H
#define DEQUE_H

#include <stddef.h>
#include <stdbool.h>

/* 链表节点 */
typedef struct my_deque_node {
    struct my_deque_node *prev;
    struct my_deque_node *next;
    void *data;
} my_deque_node_t;

/* 双端队列结构体：双向循环链表 + 长度缓存 */
typedef struct my_deque {
    my_deque_node_t head;  // 哨兵头结点(不存储数据)
    size_t size;        // 当前元素个数，O(1)读取
} my_deque_t;

/**
 * @brief 创建空双端队列
 * @return 成功返回队列指针，失败NULL
 */
my_deque_t *makeMyDeque(void);

/**
 * @brief 销毁队列，仅释放节点内存，不会释放节点内data指向内存
 * @param dq 队列指针
 */
void deleteMyDeque(my_deque_t *dq);

/**
 * @brief 判断队列是否为空
 */
#define myDequeIsEmpty(dq) ((dq) ? (dq)->head.next == &(dq)->head : true)

/**
 * @brief 获取队列元素数量
 */
#define myDequeSize(dq) ((dq) ? (dq)->size : 0)

/**
 * @brief 队尾入队（尾插）
 * @param data 用户数据指针
 * @return true成功，false内存分配失败
 */
bool myDequePushBack(my_deque_t *dq, void *data);

/**
 * @brief 队头入队（头插）
 * @param data 用户数据指针
 * @return true成功，false内存分配失败
 */
bool myDequePushFront(my_deque_t *dq, void *data);

/**
 * @brief 队头出队，返回被移除节点的data
 * @return 空队列返回NULL
 */
void *myDequePopFront(my_deque_t *dq);

/**
 * @brief 队尾出队，返回被移除节点的data
 * @return 空队列返回NULL
 */
void *myDequePopBack(my_deque_t *dq);

/**
 * @brief 获取队首元素（不删除）
 */
#define myDequeFront(dq) (myDequeIsEmpty(dq) ? NULL : (dq)->head.next->data)

/**
 * @brief 获取队尾元素（不删除）
 */
#define myDequeBack(dq) (myDequeIsEmpty(dq) ? NULL : (dq)->head.prev->data)

#endif