#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOG_FILENAME "/media/developer/temp/gles_proxy.log"

static int g_log_fd = -1;
static unsigned long long g_log_counter = 1;

/* ── Logging ─────────────────────────────────────────────────────────────── */
void init_log_file(void)
{
  if (g_log_fd >= 0)
    return;

  g_log_fd = open(LOG_FILENAME, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

void delete_log_file(void)
{
  if (g_log_fd >= 0)
  {
    close(g_log_fd);
    g_log_fd = -1;
  }

  unlink(LOG_FILENAME);

  g_log_counter = 1;
}

/* ── Atomic write helper ─────────────────────────────────────────────────── */
static inline void atomic_write_line(const char *buf, size_t len)
{
  char out[4200];
  int n = 0;

  // Format the counter as 9 digits, zero‑padded
  n = snprintf(out, sizeof(out), "%09llu ", g_log_counter++);

  // Append the original message
  if (n < sizeof(out))
  {
    memcpy(out + n, buf, len);
    n += len;
  }

  if (g_log_fd >= 0)
    write(g_log_fd, out, n);

  write(STDERR_FILENO, out, n);
}

/* ── ERROR logging ───────────────────────────────────────────────────────── */
void log_error(const char *fmt, ...)
{
  char buf[4096];
  int n = 0;

  init_log_file();

  n += snprintf(buf + n, sizeof(buf) - n, "[ERROR] ");

  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  n += snprintf(buf + n, sizeof(buf) - n, "\n");

  atomic_write_line(buf, n);
}

/* ── Console logging with prefix ─────────────────────────────────────────── */
void log_console_impl(const char *prefix, const char *fmt, ...)
{
#if defined(DEBUG) || defined(DEBUG_VERBOSE) || defined(DEBUG_DUMP_SHADERS) || \
    defined(DEBUG_OPCODES)
  char buf[4096];
  int n = 0;

#ifdef DEBUG_TO_LOGFILE
  init_log_file();
#endif

  n += snprintf(buf + n, sizeof(buf) - n, "%s ", prefix);

  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  n += snprintf(buf + n, sizeof(buf) - n, "\n");

  atomic_write_line(buf, n);
#endif
}

void log_console_always_impl(const char *prefix, const char *fmt, ...)
{
  char buf[4096];
  int n = 0;

#ifdef DEBUG_TO_LOGFILE
  init_log_file();
#endif

  n += snprintf(buf + n, sizeof(buf) - n, "%s ", prefix);

  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  n += snprintf(buf + n, sizeof(buf) - n, "\n");

  atomic_write_line(buf, n);
}
