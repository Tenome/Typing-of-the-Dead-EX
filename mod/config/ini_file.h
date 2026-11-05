#pragma once
#include <stddef.h>

typedef struct {
    char **lines;
    int count;
    int capacity;
    const char *newline;
} ini_file;

int ini_load(ini_file *ini, const char *path);
int ini_save(const ini_file *ini, const char *path);
void ini_free(ini_file *ini);
int ini_get(const ini_file *ini, const char *section, const char *key, char *value, size_t value_size);
int ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback);
int ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback);
void ini_set(ini_file *ini, const char *section, const char *key, const char *value);
void ini_set_int(ini_file *ini, const char *section, const char *key, int value);
void ini_set_bool(ini_file *ini, const char *section, const char *key, int value);
