#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <windows.h>
#include "logger.h"

static FILE              *log_fp    = NULL;
static CRITICAL_SECTION   log_cs;
static int                log_ready = 0;

int logger_init(const char *path) {
    log_fp = fopen(path, "a");
    if (!log_fp) return -1;
    InitializeCriticalSection(&log_cs);
    log_ready = 1;
    return 0;
}

void logger_close(void) {
    if (log_fp) { fclose(log_fp); log_fp = NULL; }
    if (log_ready) { DeleteCriticalSection(&log_cs); log_ready = 0; }
}

static void log_write(const char *level, const char *fmt, va_list args) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    char msg[2048];
    vsnprintf(msg, sizeof(msg), fmt, args);

    if (log_ready) EnterCriticalSection(&log_cs);
    printf("[%s] [%s] %s\n", ts, level, msg);
    fflush(stdout);
    if (log_fp) { fprintf(log_fp, "[%s] [%s] %s\n", ts, level, msg); fflush(log_fp); }
    if (log_ready) LeaveCriticalSection(&log_cs);
}

void log_info(const char *fmt, ...) {
    va_list a; va_start(a, fmt); log_write("INFO",  fmt, a); va_end(a);
}
void log_warn(const char *fmt, ...) {
    va_list a; va_start(a, fmt); log_write("WARN",  fmt, a); va_end(a);
}
void log_error(const char *fmt, ...) {
    va_list a; va_start(a, fmt); log_write("ERROR", fmt, a); va_end(a);
}
