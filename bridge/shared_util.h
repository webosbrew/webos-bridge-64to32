#pragma once

#include <stdio.h>

extern FILE *g_log;

extern void log_error(const char *fmt, ...);
extern void log_console_impl(const char *prefix, const char *fmt, ...);
extern void log_console_always_impl(const char *prefix, const char *fmt, ...);
extern void delete_log_file(void);

#ifndef LOG_PREFIX
#define LOG_PREFIX ""
#endif

#define log_console(fmt, ...) log_console_impl(LOG_PREFIX, fmt, ##__VA_ARGS__)
#define log_always(fmt, ...)                                                   \
  log_console_always_impl(LOG_PREFIX, fmt, ##__VA_ARGS__)
