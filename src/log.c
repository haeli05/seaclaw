#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>

static LogLevel g_level = LOG_INFO;
static FILE    *g_fp    = NULL;

static const char *level_names[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

void log_set_level(LogLevel level) { g_level = level; }
void log_set_file(FILE *fp)       { g_fp = fp; }

void cc_log(LogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < g_level) return;
    FILE *out = g_fp ? g_fp : stderr;

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);

    /* Strip path prefix */
    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    fprintf(out, "%s %-5s %s:%d ", ts, level_names[level], basename, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);
}
