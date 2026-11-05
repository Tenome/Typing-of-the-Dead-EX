#pragma once

typedef struct {
    char key;
    unsigned char cost;
} key_cost;

typedef struct {
    unsigned short id;
    const char *spelling;
    signed char correction;
} unit_cost;

extern const key_cost key_costs[];
extern const int key_cost_count;
extern const unit_cost unit_costs[];
extern const int unit_cost_count;

int word_difficulty(const unsigned short *units, int count);
