#pragma once
#include <stdint.h>
#include <wchar.h>

#define WORD_READING_SLOTS 64
#define WORD_MAX_READING_UNITS (WORD_READING_SLOTS - 1)
#define WORD_MAX_DISPLAY_GLYPHS 64
#define WORD_GROUP_TERMINATOR 0xFFFFFFFFu
#define WORD_CASE_ANY 0
#define WORD_CASE_UPPER 1
#define WORD_CASE_LOWER 2

typedef struct {
    uint32_t group;
    const char *display;
    uint16_t reading[WORD_READING_SLOTS];
} word_record;

#define WORD_HALF_WIDTH_CHARS (WORD_MAX_DISPLAY_GLYPHS * 2)

typedef struct {
    word_record *records;
    int count;
    const char *display_pool;
    wchar_t *half_width_text;
    uint8_t (*letter_cases)[WORD_READING_SLOTS];
} word_pack;

extern int (*word_pack_font_has_glyph)(unsigned code);
extern int word_pack_half_width;
extern int word_pack_uppercase_bonus;
extern int word_pack_skip_spaces;

int word_pack_load(const char *path, word_pack *pack);
int word_pack_load_text(char *text, const char *label, word_pack *pack);
char *word_pack_read_file(const char *path);
char *word_pack_extract_tsv_list(const char *tsv_text, const char *list_name);
void word_pack_free(word_pack *pack);
const uint8_t *word_pack_letter_cases(const word_pack *pack, const word_record *record);
int word_pack_uppercase_count(const uint8_t *letter_cases);
const wchar_t *word_pack_half_width_cell(const word_pack *pack, const void *display_cell, int *cell_index);
int word_pack_entry_difficulty(const char *display_utf8, const char *reading_utf8, int *glyph_count);
