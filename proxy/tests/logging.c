#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *g_log = NULL;

void init_log_file_test(void)
{
  if (g_log)
    return;

  char path[512];
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n < 0)
    return;

  path[n] = '\0';

  char *slash = strrchr(path, '/');
  if (slash)
    *slash = '\0';

  char full[600];
  snprintf(full, sizeof(full), "%s/gles_proxy.log", path);

  g_log = fopen(full, "w");
}

void log_console_test(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  init_log_file_test();
  if (g_log)
  {
    fprintf(g_log, "[proxy] ");
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
  }
  va_end(ap);
}

int main(int argc, char **argv)
{
  log_console_test("Hello from test.c!");

  log_console_test("Goodbye from test.c!");
}
