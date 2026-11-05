#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ini_file.h"

static char *duplicate(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void insert_line(ini_file *ini, int index, char *line) {
    if (ini->count == ini->capacity) {
        ini->capacity = ini->capacity ? ini->capacity * 2 : 64;
        ini->lines = realloc(ini->lines, ini->capacity * sizeof *ini->lines);
    }
    memmove(&ini->lines[index + 1], &ini->lines[index], (ini->count - index) * sizeof *ini->lines);
    ini->lines[index] = line;
    ini->count++;
}

int ini_load(ini_file *ini, const char *path) {
    memset(ini, 0, sizeof *ini);
    ini->newline = "\r\n";
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = malloc(size + 1);
    size_t read = fread(data, 1, size, file);
    fclose(file);
    data[read] = '\0';
    if (read && !strstr(data, "\r\n") && strchr(data, '\n')) ini->newline = "\n";
    for (char *start = data; *start;) {
        char *end = strchr(start, '\n');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        size_t content = length && start[length - 1] == '\r' ? length - 1 : length;
        insert_line(ini, ini->count, duplicate(start, content));
        if (!end) break;
        start = end + 1;
    }
    free(data);
    return 1;
}

int ini_save(const ini_file *ini, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    for (int i = 0; i < ini->count; i++) {
        fputs(ini->lines[i], file);
        fputs(ini->newline, file);
    }
    return fclose(file) == 0;
}

void ini_free(ini_file *ini) {
    for (int i = 0; i < ini->count; i++) free(ini->lines[i]);
    free(ini->lines);
    memset(ini, 0, sizeof *ini);
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ' || *text == '\t') text++;
    return text;
}

static int is_section_header(const char *line, const char *section) {
    line = skip_spaces(line);
    if (*line != '[') return 0;
    if (!section) return 1;
    size_t length = strlen(section);
    return _strnicmp(line + 1, section, length) == 0 && line[1 + length] == ']';
}

static int find_section(const ini_file *ini, const char *section) {
    for (int i = 0; i < ini->count; i++) {
        if (is_section_header(ini->lines[i], section)) return i;
    }
    return -1;
}

static const char *match_key(const char *line, const char *key) {
    line = skip_spaces(line);
    size_t length = strlen(key);
    if (_strnicmp(line, key, length) != 0) return NULL;
    const char *after = skip_spaces(line + length);
    return *after == '=' ? after + 1 : NULL;
}

static int find_key(const ini_file *ini, int section_line, const char *key, const char **value) {
    for (int i = section_line + 1; i < ini->count && !is_section_header(ini->lines[i], NULL); i++) {
        const char *found = match_key(ini->lines[i], key);
        if (found) {
            if (value) *value = found;
            return i;
        }
    }
    return -1;
}

int ini_get(const ini_file *ini, const char *section, const char *key, char *value, size_t value_size) {
    int section_line = find_section(ini, section);
    const char *found;
    if (section_line < 0 || find_key(ini, section_line, key, &found) < 0) return 0;
    snprintf(value, value_size, "%s", skip_spaces(found));
    size_t length = strlen(value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) value[--length] = '\0';
    return 1;
}

int ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback) {
    char value[64];
    return ini_get(ini, section, key, value, sizeof value) ? (int)strtol(value, NULL, 0) : fallback;
}

int ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback) {
    char value[64];
    if (!ini_get(ini, section, key, value, sizeof value)) return fallback;
    return _stricmp(value, "true") == 0 || strtol(value, NULL, 0) != 0;
}

static int end_of_section(const ini_file *ini, int section_line) {
    int end = section_line + 1;
    while (end < ini->count && !is_section_header(ini->lines[end], NULL)) end++;
    while (end > section_line + 1 && *skip_spaces(ini->lines[end - 1]) == '\0') end--;
    return end;
}

void ini_set(ini_file *ini, const char *section, const char *key, const char *value) {
    size_t length = strlen(key) + strlen(value) + 2;
    char *line = malloc(length);
    snprintf(line, length, "%s=%s", key, value);
    int section_line = find_section(ini, section);
    if (section_line < 0) {
        if (ini->count && *skip_spaces(ini->lines[ini->count - 1])) insert_line(ini, ini->count, duplicate("", 0));
        size_t header_length = strlen(section) + 3;
        char *header = malloc(header_length);
        snprintf(header, header_length, "[%s]", section);
        insert_line(ini, ini->count, header);
        insert_line(ini, ini->count, line);
        return;
    }
    int key_line = find_key(ini, section_line, key, NULL);
    if (key_line >= 0) {
        free(ini->lines[key_line]);
        ini->lines[key_line] = line;
    } else {
        insert_line(ini, end_of_section(ini, section_line), line);
    }
}

void ini_set_int(ini_file *ini, const char *section, const char *key, int value) {
    char text[16];
    snprintf(text, sizeof text, "%d", value);
    ini_set(ini, section, key, text);
}

void ini_set_bool(ini_file *ini, const char *section, const char *key, int value) {
    ini_set(ini, section, key, value ? "true" : "false");
}
