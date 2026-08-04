#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Epoll.h"

typedef struct event_loop {
    my_epoll_t *ep_;
} event_loop_t;

event_loop_t *makeEventLoop();
void eventLoopRun(event_loop_t *self);
my_epoll_t *eventLoopGetEpoll(event_loop_t *self);
void deleteEventLoop(event_loop_t *self);

#ifdef __cplusplus
}
#endif

#endif