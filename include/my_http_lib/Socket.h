#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "InetAdress.h"
#include <stdbool.h>
#include <sys/socket.h>

typedef int my_socket_t;
typedef enum {DISABLE = 0, ENABLE} my_status_t;

my_socket_t makeMySock();
int mySockGetFd(const my_socket_t self);
void mySockSetReuseAddr(my_socket_t self, bool sta);
void mySockSetReusePort(my_socket_t self, bool sta);
void mySockSetTcpNodelay(my_socket_t self, bool sta);
void mySockSetKeepAlive(my_socket_t self, bool sta);
int mySockBind(my_socket_t self, const my_inet_address_t *servaddr);
int mySockListen(my_socket_t self, const int nn);
int mySockAccept(my_socket_t self, my_inet_address_t *cliaddr);
void deleteMySock(my_socket_t self);

#ifdef __cplusplus
}
#endif