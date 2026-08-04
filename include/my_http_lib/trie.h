#ifndef TRIE_H
#define TRIE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>

// 前置声明业务数据节点
typedef struct service_element service_element_t;

// 字典树节点结构体（左子女右兄弟表示法）
typedef struct trie_node {
    char ch;                      // 当前节点代表字符
    struct trie_node *child;      // 长子节点
    struct trie_node *next;       // 兄弟节点（水平链表）
    service_element_t *data;      // 路由终点时非空，保存业务数据
} trie_node_t;

// 字典树管理结构体
typedef struct {
    trie_node_t *root;            // 虚拟根节点（不存字符）
} trie_t;

// 所有对外接口：驼峰命名
trie_t *trieCreate(void);
void trieDestroy(trie_t *tree, void (*free_data)(void *));
int trieInsert(trie_t *tree, service_element_t *data);
trie_node_t *trieSearchUrl(trie_t *tree, char *url, size_t url_len);

#ifdef __cplusplus
}
#endif

#endif