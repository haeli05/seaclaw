#ifndef CCLAW_LOG_H
#define CCLAW_LOG_H

#include <stdio.h>

typedef enum {
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

void log_set_level(LogLevel level);
void log_set_file(FILE *fp);

void cc_log(LogLevel level, const char *file, int line, const char *fmt, ...);

#define LOG_TRACE(...) cc_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) cc_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  cc_log(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  cc_log(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) cc_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) cc_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#endif
