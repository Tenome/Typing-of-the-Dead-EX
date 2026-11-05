#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "log.h"

static FILE *log_file;

void log_open(const char *path) {
    log_file = fopen(path, "w");
}

void log_line(const char *format, ...) {
    if (!log_file) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(log_file, "[%02d:%02d:%02d.%03d] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fputc('\n', log_file);
    fflush(log_file);
}
