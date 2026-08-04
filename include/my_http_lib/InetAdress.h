#ifndef INETADDRESS_H
#define INETADDRESS_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct sockaddr_in addr_;
    socklen_t len_;
} my_inet_address_t; // 表示地址协议的结构体

my_inet_address_t *makeInetAddress(const char *ip, int port);                  // 如果是监听的fd，用这个构造函数
my_inet_address_t *makeInetAddressFromSockAddr(struct sockaddr_in *addr);      // 如果是客户端连上来的fd，用这个构造函数
const char *inetAddressGetIp(const my_inet_address_t *self);                   // 返回字符串表示的ip地址，例如192.168.150.128
uint16_t inetAddressGetPort(const my_inet_address_t *self);                    // 返回整数表示的端口号：例如80, 8080
const struct sockaddr *inetAddressGetSockAddr(const my_inet_address_t *self);  // 返回addr_成员的地址，转换为sockaddr类型 
void deleteInetAddress(my_inet_address_t *self);                               // 删除inet_address_t对象

#ifdef __cplusplus
}
#endif

#endif