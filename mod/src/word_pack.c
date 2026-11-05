#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "word_pack.h"
#include "reading_units.h"
#include "log.h"
#include "glyph_tokens.h"
#include "unit_costs.h"
#include "word_mutators.h"

_Static_assert(sizeof(word_record) == 0x88, "word_record must match the Tod.exe record layout");

#define WIDE_CAPACITY 1024
#define DISPLAY_BYTES (WORD_MAX_DISPLAY_GLYPHS * 2 + 1)
#define HALF_WIDTH_STRIDE (WORD_HALF_WIDTH_CHARS + 1)
#define AUTO_GROUP "auto"
#define SCREEN_CELLS 24
#define DIFFICULTY_MASK 0xFFFFu
#define SPACE_UNIT 302
#define FULLWIDTH_ASCII_OFFSET 0xFEE0
#define CP932_QUESTION_MARK "\x81\x48"
#define FIRST_FONT_CODE 0x8140
#define LAST_FONT_CODE 0xEAA4
#define FIRST_CJK_CODEPOINT 0x2E80
#define JAPANESE_LCID MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), SORT_DEFAULT)

typedef struct {
    const char *path;
    int line_number;
} line_context;

typedef struct {
    uint16_t *units;
    uint8_t *letter_cases;
    int count;
    int truncated;
} reading_builder;

int word_pack_uppercase_bonus;
int word_pack_skip_spaces;

static char *read_whole_file(const char *path, long *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = malloc(*size + 1);
    if (data && fread(data, 1, *size, file) != (size_t)*size) {
        free(data);
        data = NULL;
    }
    fclose(file);
    if (data) data[*size] = '\0';
    return data;
}

static int utf8_to_wide(const char *text, int length, wchar_t *out) {
    if (length <= 0) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, text, length, out, WIDE_CAPACITY);
}

static int to_fullwidth_hiragana_upper(const wchar_t *text, int length, wchar_t *out) {
    wchar_t upper[WIDE_CAPACITY];
    memcpy(upper, text, length * sizeof(wchar_t));
    CharUpperBuffW(upper, length);
    return LCMapStringW(JAPANESE_LCID, LCMAP_FULLWIDTH | LCMAP_HIRAGANA, upper, length, out, WIDE_CAPACITY);
}

static int find_reading_unit(const wchar_t *text, int available, int *consumed) {
    for (int length = 2; length >= 1; length--) {
        if (length > available) continue;
        for (int i = 0; i < reading_unit_count; i++) {
            const wchar_t *candidate = reading_units[i].text;
            if ((int)wcslen(candidate) == length && wcsncmp(candidate, text, length) == 0) {
                *consumed = length;
                return reading_units[i].id;
            }
        }
    }
    return 0;
}

static int find_reading_unit_without_diacritics(wchar_t character) {
    wchar_t folded[8];
    int folded_length = FoldStringW(MAP_COMPOSITE, &character, 1, folded, 8);
    if (folded_length < 2) return 0;
    wchar_t normalised[WIDE_CAPACITY];
    int length = to_fullwidth_hiragana_upper(folded, 1, normalised);
    int consumed;
    return length == 1 ? find_reading_unit(normalised, 1, &consumed) : 0;
}

static uint8_t letter_case(wchar_t character) {
    if (IsCharUpperW(character)) return WORD_CASE_UPPER;
    if (IsCharLowerW(character)) return WORD_CASE_LOWER;
    return WORD_CASE_ANY;
}

static void append_cased_reading_unit(reading_builder *builder, int unit_id, uint8_t unit_case) {
    if (builder->count < WORD_MAX_READING_UNITS) {
        if (builder->letter_cases) builder->letter_cases[builder->count] = unit_case;
        builder->units[builder->count++] = (uint16_t)unit_id;
    } else {
        builder->truncated = 1;
    }
}

static void append_reading_unit(reading_builder *builder, int unit_id) {
    append_cased_reading_unit(builder, unit_id, WORD_CASE_ANY);
}

typedef struct {
    wchar_t character;
    const char *keys;
} transliteration;

static const transliteration letters_without_decomposition[] = {
    {0x00DF, "SS"}, {0x1E9E, "SS"}, {0x00C6, "AE"}, {0x00E6, "AE"}, {0x00D8, "O"}, {0x00F8, "O"},
    {0x0141, "L"},  {0x0142, "L"},  {0x0110, "D"},  {0x0111, "D"},  {0x00D0, "D"}, {0x00F0, "D"},
    {0x00DE, "TH"}, {0x00FE, "TH"}, {0x0152, "OE"}, {0x0153, "OE"}, {0x0131, "I"}, {0x014A, "NG"},
    {0x014B, "NG"}, {0x0166, "T"},  {0x0167, "T"},  {0x0138, "K"},  {0x017F, "S"}, {0x0126, "H"},
    {0x0127, "H"},  {0x0132, "IJ"}, {0x0133, "IJ"},
};

static int append_transliteration(reading_builder *builder, wchar_t character, uint8_t source_case) {
    for (int i = 0; i < (int)(sizeof letters_without_decomposition / sizeof letters_without_decomposition[0]); i++) {
        if (letters_without_decomposition[i].character != character) continue;
        for (const char *key = letters_without_decomposition[i].keys; *key; key++) {
            wchar_t fullwidth = (wchar_t)(*key + FULLWIDTH_ASCII_OFFSET);
            int consumed;
            int unit_id = find_reading_unit(&fullwidth, 1, &consumed);
            if (unit_id) append_cased_reading_unit(builder, unit_id, source_case);
        }
        return 1;
    }
    return 0;
}

static void append_reading_run(reading_builder *builder, const wchar_t *run, int run_length, const line_context *context) {
    wchar_t normalised[WIDE_CAPACITY];
    int length = to_fullwidth_hiragana_upper(run, run_length, normalised);
    int cases_line_up = length == run_length;
    for (int i = 0; i < length;) {
        int consumed = 1;
        int unit_id = find_reading_unit(normalised + i, length - i, &consumed);
        if (!unit_id) unit_id = find_reading_unit_without_diacritics(normalised[i]);
        uint8_t source_case = cases_line_up ? letter_case(run[i]) : WORD_CASE_ANY;
        if (unit_id) {
            append_cased_reading_unit(builder, unit_id, consumed == 1 ? source_case : WORD_CASE_ANY);
        } else if (!append_transliteration(builder, normalised[i], source_case)) {
            log_line("%s:%d: no typing unit for U+%04X, skipped", context->path, context->line_number, normalised[i]);
        }
        i += consumed;
    }
}

static int parse_unit_escape(const wchar_t *text, int available, int *unit_id, int *escape_length) {
    if (available < 3 || text[0] != L'<') return 0;
    int value = 0;
    int i = 1;
    while (i < available && i < 6 && text[i] >= L'0' && text[i] <= L'9') value = value * 10 + (text[i++] - L'0');
    if (i == 1 || i >= available || text[i] != L'>' || value <= 0 || value > 0xFFFE) return 0;
    *unit_id = value;
    *escape_length = i + 1;
    return 1;
}

static int build_reading(const wchar_t *text, int length, uint16_t *units, uint8_t *letter_cases,
                         const line_context *context) {
    reading_builder builder = {units, letter_cases, 0, 0};
    for (int i = 0; i < length;) {
        int unit_id, escape_length;
        if (parse_unit_escape(text + i, length - i, &unit_id, &escape_length)) {
            append_reading_unit(&builder, unit_id);
            i += escape_length;
            continue;
        }
        int run_end = i + 1;
        while (run_end < length && text[run_end] != L'<') run_end++;
        append_reading_run(&builder, text + i, run_end - i, context);
        i = run_end;
    }
    if (builder.truncated) {
        log_line("%s:%d: reading longer than %d units, truncated", context->path, context->line_number, WORD_MAX_READING_UNITS);
    }
    return builder.count;
}

static wchar_t display_substitute(wchar_t character) {
    if (character == 0xFF02) return 0x201D;
    if (character == 0xFF07) return 0x2019;
    return character;
}

int (*word_pack_font_has_glyph)(unsigned code);

static int encode_font_glyph(wchar_t character, char *out) {
    BOOL used_default = FALSE;
    int written = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &character, 1, out, 2, NULL, &used_default);
    if (written != 2 || used_default) return 0;
    unsigned code = ((unsigned char)out[0] << 8) | (unsigned char)out[1];
    if (code < FIRST_FONT_CODE || code > LAST_FONT_CODE) return 0;
    return !word_pack_font_has_glyph || word_pack_font_has_glyph(code);
}

static int encode_token_glyph(uint32_t codepoint, char *out) {
    unsigned token = glyph_token_for_codepoint(codepoint);
    if (!token) return 0;
    out[0] = (char)(token >> 8);
    out[1] = (char)(token & 0xFF);
    return 1;
}

static uint32_t next_codepoint(const wchar_t *text, int length, int *i) {
    uint32_t unit = text[(*i)++];
    if (unit >= 0xD800 && unit <= 0xDBFF && *i < length && text[*i] >= 0xDC00 && text[*i] <= 0xDFFF) {
        return 0x10000 + ((unit - 0xD800) << 10) + (text[(*i)++] - 0xDC00);
    }
    return unit;
}

static int encode_font_codepoint(uint32_t codepoint, char *out) {
    if (codepoint > 0xFFFF) return 0;
    wchar_t character = (wchar_t)codepoint, fullwidth;
    if (LCMapStringW(JAPANESE_LCID, LCMAP_FULLWIDTH, &character, 1, &fullwidth, 1) != 1) fullwidth = character;
    return encode_font_glyph(display_substitute(fullwidth), out);
}

static int keeps_native_glyph_in_token_word(uint32_t codepoint) {
    return codepoint == ' ' || codepoint >= FIRST_CJK_CODEPOINT;
}

static int display_needs_tokens(const wchar_t *text, int length) {
    char scratch[2];
    for (int i = 0; i < length;) {
        if (!encode_font_codepoint(next_codepoint(text, length, &i), scratch)) return 1;
    }
    return 0;
}

int word_pack_half_width;

static int precompose(const wchar_t *text, int length, wchar_t *out) {
    int precomposed_length = FoldStringW(MAP_PRECOMPOSED, text, length, out, WIDE_CAPACITY);
    if (precomposed_length) return precomposed_length;
    memcpy(out, text, length * sizeof(wchar_t));
    return length;
}

static int half_width_text(const wchar_t *precomposed, int length, wchar_t *out) {
    if (length <= 0) return 0;
    int halfwidth_length = LCMapStringW(JAPANESE_LCID, LCMAP_HALFWIDTH, precomposed, length, out, WIDE_CAPACITY);
    for (int i = 0; i < halfwidth_length; i++) {
        if (out[i] >= FIRST_CJK_CODEPOINT || (out[i] >= 0xD800 && out[i] <= 0xDFFF) || out[i] < ' ') return 0;
    }
    if (!word_pack_half_width && halfwidth_length <= SCREEN_CELLS) return 0;
    if (halfwidth_length > WORD_HALF_WIDTH_CHARS) halfwidth_length = WORD_HALF_WIDTH_CHARS;
    out[halfwidth_length] = 0;
    return halfwidth_length;
}

static int half_width_cells(int characters) {
    return (characters + 1) / 2;
}

static int encode_half_width_cells(int characters, char *out) {
    int cells = half_width_cells(characters);
    for (int i = 0; i < cells; i++) {
        out[i * 2] = (char)(GLYPH_HALF_WIDTH_CELL >> 8);
        out[i * 2 + 1] = (char)(GLYPH_HALF_WIDTH_CELL & 0xFF);
    }
    out[cells * 2] = '\0';
    return cells * 2;
}

static int encode_display(const wchar_t *text, int length, char *out, wchar_t *half_width_out,
                          const line_context *context) {
    wchar_t precomposed[WIDE_CAPACITY];
    int precomposed_length = precompose(text, length, precomposed);
    wchar_t narrow[WIDE_CAPACITY];
    int narrow_length = half_width_text(precomposed, precomposed_length, narrow);
    half_width_out[0] = 0;
    if (narrow_length) {
        memcpy(half_width_out, narrow, (narrow_length + 1) * sizeof(wchar_t));
        return encode_half_width_cells(narrow_length, out);
    }
    int token_word = display_needs_tokens(precomposed, precomposed_length);
    int bytes = 0, glyphs = 0;
    for (int i = 0; i < precomposed_length;) {
        if (glyphs == WORD_MAX_DISPLAY_GLYPHS) {
            log_line("%s:%d: display longer than %d characters, truncated", context->path, context->line_number,
                     WORD_MAX_DISPLAY_GLYPHS);
            break;
        }
        uint32_t codepoint = next_codepoint(precomposed, precomposed_length, &i);
        int encoded = (!token_word || keeps_native_glyph_in_token_word(codepoint)) &&
                      encode_font_codepoint(codepoint, out + bytes);
        if (!encoded) encoded = encode_token_glyph(codepoint, out + bytes);
        if (!encoded) {
            log_line("%s:%d: no glyph token left for U+%04X, shown as ?", context->path, context->line_number, codepoint);
            memcpy(out + bytes, CP932_QUESTION_MARK, 2);
        }
        bytes += 2;
        glyphs++;
    }
    out[bytes] = '\0';
    return bytes;
}

static int count_lines(const char *text) {
    int lines = 1;
    for (const char *p = text; *p; p++) lines += *p == '\n';
    return lines;
}

static char *next_field(char **cursor) {
    char *start = *cursor;
    if (!start) return NULL;
    char *tab = strchr(start, '\t');
    if (tab) {
        *tab = '\0';
        *cursor = tab + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

static int parse_group_field(const char *field, unsigned long *group) {
    if (_stricmp(field, AUTO_GROUP) == 0) {
        *group = 0;
        return 1;
    }
    char *end;
    *group = strtoul(field, &end, 10);
    return end != field && !*end;
}

static int remove_space_units(uint16_t *units, uint8_t *letter_cases, int count) {
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (units[i] == SPACE_UNIT) continue;
        units[kept] = units[i];
        letter_cases[kept] = letter_cases[i];
        kept++;
    }
    for (int i = kept; i < count; i++) {
        units[i] = 0;
        letter_cases[i] = WORD_CASE_ANY;
    }
    return kept;
}

static int parse_word_line(char *line, word_record *record, char *display_out, wchar_t *half_width_out,
                           uint8_t *letter_cases, const line_context *context) {
    char *cursor = line;
    char *first = next_field(&cursor);
    char *second = next_field(&cursor);
    char *third = next_field(&cursor);
    unsigned long group = 0;
    char *display_field = first, *reading_field = second;
    if (second && *second && parse_group_field(first, &group)) {
        display_field = second;
        reading_field = third;
    }
    if (!display_field || !*display_field) {
        log_line("%s:%d: no display text, skipped", context->path, context->line_number);
        return 0;
    }
    if (!reading_field || !*reading_field) reading_field = display_field;

    wchar_t display_wide[WIDE_CAPACITY], reading_wide[WIDE_CAPACITY];
    int display_length = utf8_to_wide(display_field, (int)strlen(display_field), display_wide);
    int reading_length = utf8_to_wide(reading_field, (int)strlen(reading_field), reading_wide);
    if (!display_length || !reading_length) {
        log_line("%s:%d: invalid UTF-8 or line too long, skipped", context->path, context->line_number);
        return 0;
    }
    if (word_mutators_active()) mutate_word_text(display_wide, display_length, reading_wide, reading_length);

    memset(record, 0, sizeof *record);
    memset(letter_cases, WORD_CASE_ANY, WORD_READING_SLOTS);
    int unit_count = build_reading(reading_wide, reading_length, record->reading, letter_cases, context);
    if (!unit_count) {
        log_line("%s:%d: reading has no typable units, skipped", context->path, context->line_number);
        return 0;
    }
    record->group = group ? (uint32_t)group : (uint32_t)word_difficulty(record->reading, unit_count);
    if (word_pack_uppercase_bonus && (record->group & DIFFICULTY_MASK)) {
        uint32_t level = (record->group & DIFFICULTY_MASK) +
                         (uint32_t)(word_pack_uppercase_bonus * word_pack_uppercase_count(letter_cases));
        record->group = (record->group & ~DIFFICULTY_MASK) | (level > DIFFICULTY_MASK ? DIFFICULTY_MASK : level);
    }
    if (word_pack_skip_spaces) unit_count = remove_space_units(record->reading, letter_cases, unit_count);
    if (!unit_count) return 0;
    encode_display(display_wide, display_length, display_out, half_width_out, context);
    record->display = display_out;
    return 1;
}

int word_pack_load_text(char *text, const char *label, word_pack *pack) {
    int capacity = count_lines(text);
    size_t records_bytes = (size_t)(capacity + 1) * sizeof(word_record);
    char *block = malloc(records_bytes + (size_t)capacity * DISPLAY_BYTES);
    wchar_t *half_width_pool = malloc((size_t)capacity * HALF_WIDTH_STRIDE * sizeof(wchar_t));
    uint8_t (*letter_cases)[WORD_READING_SLOTS] = malloc((size_t)(capacity + 1) * WORD_READING_SLOTS);
    if (!block || !half_width_pool || !letter_cases) {
        free(block);
        free(half_width_pool);
        free(letter_cases);
        return 0;
    }
    word_record *records = (word_record *)block;
    char *display_pool = block + records_bytes;

    char *line = text;
    if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line += 3;
    line_context context = {label, 0};
    int count = 0;
    while (line) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        size_t length = strlen(line);
        if (length && line[length - 1] == '\r') line[--length] = '\0';
        context.line_number++;
        if (length && line[0] != '#') {
            count += parse_word_line(line, &records[count], display_pool + (size_t)count * DISPLAY_BYTES,
                                     half_width_pool + (size_t)count * HALF_WIDTH_STRIDE, letter_cases[count], &context);
        }
        line = newline ? newline + 1 : NULL;
    }

    memset(&records[count], 0, sizeof(word_record));
    records[count].group = WORD_GROUP_TERMINATOR;
    pack->records = records;
    pack->count = count;
    pack->display_pool = display_pool;
    pack->half_width_text = half_width_pool;
    pack->letter_cases = letter_cases;
    return 1;
}

int word_pack_load(const char *path, word_pack *pack) {
    long size;
    char *text = read_whole_file(path, &size);
    if (!text) return 0;
    int loaded = word_pack_load_text(text, path, pack);
    free(text);
    return loaded;
}

char *word_pack_read_file(const char *path) {
    long size;
    return read_whole_file(path, &size);
}

static const char *tsv_field_end(const char *field) {
    const char *end = field;
    while (*end && *end != '\t' && *end != '\n' && *end != '\r') end++;
    return end;
}

static int tsv_list_matches(const char *line, const char *list_name, const char **after) {
    size_t length = strlen(list_name);
    if (_strnicmp(line, list_name, length) != 0) return 0;
    const char *rest = line + length;
    if (_strnicmp(rest, ".bin", 4) == 0) rest += 4;
    if (*rest != '\t') return 0;
    *after = rest + 1;
    return 1;
}

static void append_text(char **buffer, size_t *used, size_t *capacity, const char *text, size_t length) {
    if (*used + length + 1 > *capacity) {
        *capacity = (*used + length + 1) * 2;
        *buffer = realloc(*buffer, *capacity);
    }
    memcpy(*buffer + *used, text, length);
    *used += length;
    (*buffer)[*used] = '\0';
}

char *word_pack_extract_tsv_list(const char *tsv_text, const char *list_name) {
    char *out = NULL;
    size_t used = 0, capacity = 0;
    for (const char *line = tsv_text; line && *line;) {
        const char *fields;
        if (*line != '#' && tsv_list_matches(line, list_name, &fields)) {
            const char *difficulty_end = tsv_field_end(fields);
            const char *display = *difficulty_end == '\t' ? difficulty_end + 1 : difficulty_end;
            const char *display_end = tsv_field_end(display);
            const char *reading = *display_end == '\t' ? display_end + 1 : display_end;
            const char *reading_end = tsv_field_end(reading);
            if (display_end > display) {
                if (difficulty_end > fields) append_text(&out, &used, &capacity, fields, difficulty_end - fields);
                else append_text(&out, &used, &capacity, AUTO_GROUP, strlen(AUTO_GROUP));
                append_text(&out, &used, &capacity, "\t", 1);
                append_text(&out, &used, &capacity, display, display_end - display);
                if (reading_end > reading) {
                    append_text(&out, &used, &capacity, "\t", 1);
                    append_text(&out, &used, &capacity, reading, reading_end - reading);
                }
                append_text(&out, &used, &capacity, "\n", 1);
            }
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return out;
}

const wchar_t *word_pack_half_width_cell(const word_pack *pack, const void *display_cell, int *cell_index) {
    if (!pack->records || !pack->half_width_text) return NULL;
    const char *cell = display_cell;
    if (cell < pack->display_pool || cell >= pack->display_pool + (size_t)pack->count * DISPLAY_BYTES) return NULL;
    size_t offset = (size_t)(cell - pack->display_pool);
    const wchar_t *text = pack->half_width_text + offset / DISPLAY_BYTES * HALF_WIDTH_STRIDE;
    *cell_index = (int)(offset % DISPLAY_BYTES / 2);
    return text[0] ? text : NULL;
}

int word_pack_entry_difficulty(const char *display_utf8, const char *reading_utf8, int *glyph_count) {
    line_context context = {"word list", 0};
    const char *reading = reading_utf8 && *reading_utf8 ? reading_utf8 : display_utf8;
    wchar_t display_wide[WIDE_CAPACITY], reading_wide[WIDE_CAPACITY], precomposed[WIDE_CAPACITY];
    int display_length = utf8_to_wide(display_utf8, (int)strlen(display_utf8), display_wide);
    int reading_length = utf8_to_wide(reading, (int)strlen(reading), reading_wide);
    if (!display_length || !reading_length) return 0;
    int precomposed_length = precompose(display_wide, display_length, precomposed);
    wchar_t narrow[WIDE_CAPACITY];
    int narrow_length = half_width_text(precomposed, precomposed_length, narrow);
    *glyph_count = 0;
    if (narrow_length) {
        *glyph_count = half_width_cells(narrow_length);
    } else {
        for (int i = 0; i < precomposed_length; (*glyph_count)++) next_codepoint(precomposed, precomposed_length, &i);
    }
    uint16_t units[WORD_READING_SLOTS] = {0};
    int unit_count = build_reading(reading_wide, reading_length, units, NULL, &context);
    return unit_count ? word_difficulty(units, unit_count) : 0;
}

const uint8_t *word_pack_letter_cases(const word_pack *pack, const word_record *record) {
    if (!pack->records || !pack->letter_cases || record < pack->records || record >= pack->records + pack->count) return NULL;
    return pack->letter_cases[record - pack->records];
}

int word_pack_uppercase_count(const uint8_t *letter_cases) {
    int count = 0;
    for (int i = 0; i < WORD_READING_SLOTS; i++) count += letter_cases[i] == WORD_CASE_UPPER;
    return count;
}

void word_pack_free(word_pack *pack) {
    free(pack->letter_cases);
    pack->letter_cases = NULL;
    free(pack->half_width_text);
    pack->half_width_text = NULL;
    free(pack->records);
    pack->records = NULL;
    pack->count = 0;
}
