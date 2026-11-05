#pragma once
#include <wchar.h>

typedef struct {
    const wchar_t *text;
    unsigned short id;
} reading_unit;

extern const reading_unit reading_units[];
extern const int reading_unit_count;
