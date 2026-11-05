#pragma once

typedef struct {
    const char *name;
    unsigned char kids;
    unsigned char keep_original;
    unsigned int first_slot;
    unsigned short slot_count;
} template_list;

extern const unsigned short template_difficulties[];
extern const template_list template_lists[];
extern const int template_list_count;
