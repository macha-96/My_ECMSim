#ifndef MY_SIMPLE_SERVER_LOG_H
#define MY_SIMPLE_SERVER_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <time.h>

// 日志级别定义
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

static const char* log_level_str[] = {
    "[DEBUG]",
    "[INFO] ",
    "[WARN] ",
    "[ERROR]",
    "[FATAL]"
};

/**
 * 底层日志打印宏
 * 格式示例：
 * [2026-08-03 22:10:30] [INFO] main.c:42 server start, listen port=8080
 */
#define LOG_BASE(level, fmt, ...) do {                                 \
    time_t now = time(NULL);                                            \
    struct tm* t = localtime(&now);                                     \
    fprintf(stdout,                                                     \
        "[%04d-%02d-%02d %02d:%02d:%02d] %s %s:%d " fmt "\n",           \
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,                   \
        t->tm_hour, t->tm_min, t->tm_sec,                               \
        log_level_str[level], __FILE__, __LINE__, ##__VA_ARGS__);       \
} while(0)

// 对外简易宏
#define LOG_DEBUG(fmt, ...) LOG_BASE(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG_BASE(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG_BASE(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_BASE(LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) LOG_BASE(LOG_FATAL, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif