#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "dictionary_hooks.h"
#include "word_pack.h"
#include "unit_costs.h"
#include "word_mutators.h"
#include "log.h"
#include "MinHook.h"

#define LOAD_WORD_ITEM_VA 0x004cb3a0
#define COUNT_WORD_RECORDS_VA 0x004cb820
#define GET_WORD_RECORD_VA 0x004cb890

#define WORD_BANK_NUMBER (*(int *)0x007808ec)
#define LOADED_ITEM_INDEX (*(unsigned short *)0x00547e84)
#define RECORDS_NEED_RELOCATION (*(unsigned char *)0x008168f8)
#define HAB_RECORDS ((const word_record *)0x007808f8)
#define WORD_FILE_NAMES (*(const char *const **)0x004d74f8)
#define WORD_KIDS_FILE_NAMES (*(const char *const **)0x004d7588)

#define MAIN_WORD_BANK 6
#define LOADED_WORD_FILE_KIND 2
#define MAX_PACKS 16
#define PACK_NAME_LENGTH 64
#define MAIN_TSV "words.tsv"
#define SEEN_WORD_SLOTS 8192
#define KIDS_TSV "kids.tsv"

#define DIFFICULTY_MASK 0xFFFFu
#define NOT_A_KIND (-1)

typedef struct {
    const char *list_name;
    DWORD address;
    int slots;
    int kind;
} builtin_table;

static const builtin_table builtin_tables[] = {
    {"builtin_letters", 0x00501fa8, 25, 0},
    {"builtin_kana", 0x00502cf0, 40, 1},
    {"builtin_staff_stage", 0x00504f78, 27, 3},
    {"builtin_staff_kanji", 0x00505dd0, 27, 4},
    {"builtin_staff_kana", 0x00506cb0, 24, 5},
    {"builtin_short_words", 0x005079f8, 89, 6},
    {"builtin_drill", 0x00513140, 28, NOT_A_KIND},
};
#define BUILTIN_TABLE_COUNT ((int)(sizeof builtin_tables / sizeof builtin_tables[0]))
#define MAX_BUILTIN_SLOTS 89
#define DEFAULT_UPPERCASE_BONUS 4
#define PICK_WORD_VA 0x004c6dd0
#define DEFAULT_IN_ORDER_MIN_LENGTH 4
#define FIRST_STAFF_KIND 3
#define LAST_STAFF_KIND 5
#define SEQUENCE_FILE "sequence.txt"
#define SEQUENCE_POSITION_FILE "sequence_position.txt"

static const DWORD random_pick_return_addresses[] = {0x004c71c8, 0x004c7228, 0x004c7276, 0x004c733e};

typedef int(__cdecl *load_word_item_fn)(unsigned int index);
typedef int(__cdecl *count_word_records_fn)(int kind);
typedef const word_record *(__cdecl *get_word_record_fn)(int kind, int index);
typedef void(__cdecl *pick_word_fn)(void *word);

static load_word_item_fn original_load_word_item;
static count_word_records_fn original_count_word_records;
static get_word_record_fn original_get_word_record;
static pick_word_fn original_pick_word;

static char dictionary_folder[MAX_PATH];
static char pack_names[MAX_PACKS][PACK_NAME_LENGTH];
static int pack_count;
static int log_word_sources;
static uint32_t seen_words[SEEN_WORD_SLOTS];
static char *pack_tsv[MAX_PACKS][2];
static int pack_tsv_checked[MAX_PACKS][2];
static int verify_against_hab;
static int mix_packs;

#define QUIZ_LIST_RECORDS 19
#define QUIZ_QUESTION_DIFFICULTY 12
#define QUIZ_CORRECT_ANSWERS 8
static word_pack active_pack;
static word_pack builtin_packs[BUILTIN_TABLE_COUNT];
static int builtin_tables_replaced;
static const uint8_t *builtin_slot_cases[BUILTIN_TABLE_COUNT][MAX_BUILTIN_SLOTS];
static int active_bank = -1;
static int active_index = -1;
static word_pack sequence_pack;
static int sequence_position;
static int sequence_handed_out;
static int in_order_min_length = DEFAULT_IN_ORDER_MIN_LENGTH;
static char sequence_position_path[MAX_PATH];

static void read_pack_list(const char *ini_path) {
    char list[MAX_PACKS * PACK_NAME_LENGTH];
    GetPrivateProfileStringA("dictionary", "packs", "", list, sizeof list, ini_path);
    verify_against_hab = GetPrivateProfileIntA("dictionary", "verify", 0, ini_path);
    mix_packs = GetPrivateProfileIntA("dictionary", "mix", 0, ini_path);
    for (char *name = strtok(list, ","); name && pack_count < MAX_PACKS; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (*name) lstrcpynA(pack_names[pack_count++], name, PACK_NAME_LENGTH);
    }
}

static const char *word_item_name(int bank, int index) {
    return bank == MAIN_WORD_BANK ? WORD_FILE_NAMES[index] : WORD_KIDS_FILE_NAMES[index];
}

static const char *pack_tsv_text(int pack, int bank) {
    int kids = bank != MAIN_WORD_BANK;
    if (!pack_tsv_checked[pack][kids]) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s%s\\%s", dictionary_folder, pack_names[pack], kids ? KIDS_TSV : MAIN_TSV);
        pack_tsv[pack][kids] = word_pack_read_file(path);
        pack_tsv_checked[pack][kids] = 1;
        if (pack_tsv[pack][kids]) log_line("dictionary: pack %s uses %s", pack_names[pack], path);
    }
    return pack_tsv[pack][kids];
}

static int accept_loaded_pack(int loaded, const char *item_name, const char *source) {
    if (!loaded) {
        log_line("dictionary: cannot read %s", source);
        return 0;
    }
    if (active_pack.count == 0) {
        log_line("dictionary: %s has no usable words in %s", item_name, source);
        word_pack_free(&active_pack);
        return 0;
    }
    log_line("dictionary: %s -> %s (%d words)", item_name, source, active_pack.count);
    return 1;
}

static char *pack_list_text(int pack, int bank, const char *item_name, char *source, size_t source_size) {
    const char *subfolder = bank == MAIN_WORD_BANK ? "" : "kids\\";
    snprintf(source, source_size, "%s%s\\%s%s.txt", dictionary_folder, pack_names[pack], subfolder, item_name);
    char *text = word_pack_read_file(source);
    if (text) return text;
    const char *tsv = pack_tsv_text(pack, bank);
    snprintf(source, source_size, "%s\\%s", pack_names[pack], bank == MAIN_WORD_BANK ? MAIN_TSV : KIDS_TSV);
    return tsv ? word_pack_extract_tsv_list(tsv, item_name) : NULL;
}

static void append_list_text(char **mixed, size_t *mixed_length, const char *text) {
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text += 3;
    size_t length = strlen(text);
    char *grown = realloc(*mixed, *mixed_length + length + 2);
    if (!grown) return;
    memcpy(grown + *mixed_length, text, length);
    *mixed_length += length;
    grown[(*mixed_length)++] = '\n';
    grown[*mixed_length] = '\0';
    *mixed = grown;
}

static char *mixed_list_text(int bank, const char *list_name, char *source, size_t source_size) {
    char *mixed = NULL;
    size_t mixed_length = 0;
    source[0] = '\0';
    for (int i = 0; i < pack_count; i++) {
        char pack_source[MAX_PATH];
        char *text = pack_list_text(i, bank, list_name, pack_source, sizeof pack_source);
        if (!text) continue;
        append_list_text(&mixed, &mixed_length, text);
        free(text);
        size_t used = strlen(source);
        snprintf(source + used, source_size - used, "%s%s", used ? " + " : "mixed: ", pack_names[i]);
    }
    return mixed;
}

static int load_item_from_packs(int bank, const char *item_name) {
    char source[MAX_PATH * 2];
    if (mix_packs) {
        char *mixed = mixed_list_text(bank, item_name, source, sizeof source);
        if (!mixed) return 0;
        int loaded = word_pack_load_text(mixed, source, &active_pack);
        free(mixed);
        return accept_loaded_pack(loaded, item_name, source);
    }
    for (int i = 0; i < pack_count; i++) {
        char *text = pack_list_text(i, bank, item_name, source, sizeof source);
        if (!text) continue;
        int loaded = word_pack_load_text(text, source, &active_pack);
        free(text);
        if (accept_loaded_pack(loaded, item_name, source)) return 1;
    }
    return 0;
}

static int count_hab_records(void) {
    int count = 0;
    while (HAB_RECORDS[count].group != WORD_GROUP_TERMINATOR && HAB_RECORDS[count].reading[0]) count++;
    return count;
}

static void verify_pack_against_hab(const char *item_name) {
    int hab_count = count_hab_records();
    int mismatches = 0;
    if (hab_count != active_pack.count) {
        log_line("verify %s: HAB has %d words, pack has %d", item_name, hab_count, active_pack.count);
    }
    int compared = hab_count < active_pack.count ? hab_count : active_pack.count;
    for (int i = 0; i < compared; i++) {
        const word_record *hab = &HAB_RECORDS[i];
        const word_record *pack = &active_pack.records[i];
        const char *hab_display = (const char *)HAB_RECORDS + (size_t)hab->display;
        if (hab->group == pack->group && !memcmp(hab->reading, pack->reading, sizeof hab->reading) &&
            !strcmp(hab_display, pack->display)) {
            continue;
        }
        if (mismatches++ < 10) {
            log_line("verify %s: word %d differs (group %u/%u, display '%s'/'%s')", item_name, i, hab->group,
                     pack->group, hab_display, pack->display);
        }
    }
    log_line("verify %s: %d of %d words match", item_name, compared - mismatches, compared);
}

static void select_word_item(int bank, int index) {
    if (bank == active_bank && index == active_index) return;
    word_pack_free(&active_pack);
    active_bank = bank;
    active_index = index;
    const char *item_name = word_item_name(bank, index);
    if (!load_item_from_packs(bank, item_name)) {
        log_line("dictionary: %s from %s", item_name, bank == MAIN_WORD_BANK ? "word.HAB" : "wordkids.HAB");
        return;
    }
    if (verify_against_hab && !RECORDS_NEED_RELOCATION) {
        log_line("verify %s: skipped, HAB item was not reloaded", item_name);
    } else if (verify_against_hab) {
        verify_pack_against_hab(item_name);
    }
}

static int reading_length(const word_record *record) {
    int length = 0;
    while (length < WORD_MAX_READING_UNITS && record->reading[length]) length++;
    return length;
}

static uint32_t scaled_builtin_group(const word_record *original, const word_record *replacement,
                                     const uint8_t *replacement_cases) {
    int original_cost = word_difficulty(original->reading, reading_length(original));
    int replacement_cost = word_difficulty(replacement->reading, reading_length(replacement)) +
                           word_pack_uppercase_bonus * word_pack_uppercase_count(replacement_cases);
    uint32_t original_level = original->group & DIFFICULTY_MASK;
    uint32_t level = original_level;
    if (original_cost > 0 && replacement_cost > 0) {
        level = (original_level * (uint32_t)replacement_cost + (uint32_t)original_cost / 2) / (uint32_t)original_cost;
        if (level == 0 && original_level) level = 1;
    }
    return (original->group & ~DIFFICULTY_MASK) | (level & DIFFICULTY_MASK);
}

static int load_builtin_pack(const builtin_table *table, word_pack *pack, char *source, size_t source_size) {
    if (mix_packs) {
        char *mixed = mixed_list_text(MAIN_WORD_BANK, table->list_name, source, source_size);
        if (!mixed) return 0;
        int loaded = word_pack_load_text(mixed, source, pack);
        free(mixed);
        if (loaded && pack->count > 0) return 1;
        word_pack_free(pack);
        return 0;
    }
    for (int i = 0; i < pack_count; i++) {
        const char *tsv = pack_tsv_text(i, MAIN_WORD_BANK);
        char *lines = tsv ? word_pack_extract_tsv_list(tsv, table->list_name) : NULL;
        if (!lines) continue;
        snprintf(source, source_size, "%s\\%s", pack_names[i], MAIN_TSV);
        int loaded = word_pack_load_text(lines, source, pack);
        free(lines);
        if (loaded && pack->count > 0) return 1;
        word_pack_free(pack);
    }
    return 0;
}

static int table_holds_credit_groups(const builtin_table *table) {
    return table->kind >= FIRST_STAFF_KIND && table->kind <= LAST_STAFF_KIND;
}

static void replace_builtin_table(int table_index) {
    const builtin_table *table = &builtin_tables[table_index];
    word_pack *pack = &builtin_packs[table_index];
    char source[MAX_PATH];
    if (!load_builtin_pack(table, pack, source, sizeof source)) return;
    word_record *slots = (word_record *)table->address;
    DWORD old_protection;
    if (!VirtualProtect(slots, sizeof(word_record) * table->slots, PAGE_READWRITE, &old_protection)) {
        log_line("dictionary: cannot write built-in table %s", table->list_name);
        word_pack_free(pack);
        return;
    }
    for (int slot = 0; slot < table->slots; slot++) {
        int index = mix_packs && pack->count > table->slots ? slot * pack->count / table->slots : slot % pack->count;
        const word_record *replacement = &pack->records[index];
        builtin_slot_cases[table_index][slot] = word_pack_letter_cases(pack, replacement);
        if (!table_holds_credit_groups(table))
            slots[slot].group = scaled_builtin_group(&slots[slot], replacement, builtin_slot_cases[table_index][slot]);
        slots[slot].display = replacement->display;
        memcpy(slots[slot].reading, replacement->reading, sizeof slots[slot].reading);
    }
    VirtualProtect(slots, sizeof(word_record) * table->slots, old_protection, &old_protection);
    log_line("dictionary: %s (%d slots) -> %s (%d words%s)", table->list_name, table->slots, source, pack->count,
             pack->count > table->slots ? (mix_packs ? ", spread over the slots" : ", extra words unused")
             : pack->count < table->slots ? ", repeated" : "");
}

static void replace_builtin_tables_once(void) {
    if (builtin_tables_replaced) return;
    builtin_tables_replaced = 1;
    for (int i = 0; i < BUILTIN_TABLE_COUNT; i++) replace_builtin_table(i);
}

static int builtin_kind_replaced(int kind) {
    int table_kind = kind >= 1 && kind <= 6 && kind != LOADED_WORD_FILE_KIND ? kind : 0;
    for (int i = 0; i < BUILTIN_TABLE_COUNT; i++) {
        if (builtin_tables[i].kind == table_kind) return builtin_packs[i].records != NULL;
    }
    return 0;
}

static int __cdecl load_word_item_hook(unsigned int index) {
    replace_builtin_tables_once();
    int result = original_load_word_item(index);
    select_word_item(WORD_BANK_NUMBER, LOADED_ITEM_INDEX);
    return result;
}

static int __cdecl count_word_records_hook(int kind) {
    replace_builtin_tables_once();
    if (kind == LOADED_WORD_FILE_KIND && active_pack.records) return active_pack.count;
    return original_count_word_records(kind);
}

static int first_time_seen(uint32_t key) {
    uint32_t slot = (key * 2654435761u) % SEEN_WORD_SLOTS;
    for (int probe = 0; probe < SEEN_WORD_SLOTS; probe++, slot = (slot + 1) % SEEN_WORD_SLOTS) {
        if (seen_words[slot] == key) return 0;
        if (!seen_words[slot]) {
            seen_words[slot] = key;
            return 1;
        }
    }
    return 0;
}

static void log_game_word_source(int kind, int index, const word_record *record) {
    uint32_t list = kind == LOADED_WORD_FILE_KIND ? (uint32_t)(active_bank * 1000 + active_index) : 0;
    if (!record || !first_time_seen(((uint32_t)kind << 28) ^ (list << 12) ^ (uint32_t)index ^ 0x80000000u)) return;
    wchar_t wide[WORD_MAX_DISPLAY_GLYPHS * 2 + 1] = L"";
    char text[WORD_MAX_DISPLAY_GLYPHS * 6 + 1] = "";
    if (record->display && !IsBadStringPtrA(record->display, WORD_MAX_DISPLAY_GLYPHS * 2)) {
        MultiByteToWideChar(932, 0, record->display, -1, wide, WORD_MAX_DISPLAY_GLYPHS * 2);
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof text, NULL, NULL);
    }
    if (kind == LOADED_WORD_FILE_KIND) {
        log_line("game word: %s %s #%d (group %u): %s", active_bank == MAIN_WORD_BANK ? "word.HAB" : "wordkids.HAB",
                 active_index >= 0 ? word_item_name(active_bank, active_index) : "?", index, record->group, text);
    } else {
        log_line("game word: built-in table %d #%d (group %u): %s", kind, index, record->group, text);
    }
}

static int is_random_pick(const void *return_address) {
    for (size_t i = 0; i < sizeof random_pick_return_addresses / sizeof random_pick_return_addresses[0]; i++) {
        if ((DWORD)(uintptr_t)return_address == random_pick_return_addresses[i]) return 1;
    }
    return 0;
}

static const word_record *next_in_order(const word_record *game_choice) {
    if (!game_choice || reading_length(game_choice) < in_order_min_length) return game_choice;
    sequence_handed_out = 1;
    return &sequence_pack.records[sequence_position];
}

static void save_sequence_position(void) {
    FILE *file = fopen(sequence_position_path, "w");
    if (!file) return;
    fprintf(file, "%d\n", sequence_position);
    fclose(file);
}

static void __cdecl pick_word_hook(void *word) {
    sequence_handed_out = 0;
    original_pick_word(word);
    if (!sequence_handed_out) return;
    sequence_position = (sequence_position + 1) % sequence_pack.count;
    save_sequence_position();
}

static const word_record *__cdecl get_word_record_hook(int kind, int index) {
    replace_builtin_tables_once();
    int in_order = sequence_pack.count && kind == LOADED_WORD_FILE_KIND && is_random_pick(__builtin_return_address(0));
    if (kind == LOADED_WORD_FILE_KIND && active_pack.records && index >= 0 && index < active_pack.count) {
        return in_order ? next_in_order(&active_pack.records[index]) : &active_pack.records[index];
    }
    const word_record *record = original_get_word_record(kind, index);
    if (in_order) return next_in_order(record);
    if (log_word_sources && !builtin_kind_replaced(kind)) log_game_word_source(kind, index, record);
    return record;
}

static int install_hook(DWORD address, void *hook, void **original, const char *name) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, original);
    if (status == MH_OK) status = MH_EnableHook((void *)address);
    log_line("hook %s at %08lX: %s", name, address, MH_StatusToString(status));
    return status == MH_OK;
}

int dictionary_quiz_answer(const uint16_t *reading) {
    const word_record *record = (const word_record *)((const char *)reading - offsetof(word_record, reading));
    if (!active_pack.records || active_pack.count != QUIZ_LIST_RECORDS || record < active_pack.records ||
        record >= active_pack.records + active_pack.count || (active_pack.records[0].group & 0xffff) != QUIZ_QUESTION_DIFFICULTY)
        return -1;
    int position = (int)(record - active_pack.records);
    return position >= 1 && position <= QUIZ_CORRECT_ANSWERS;
}

const uint8_t *dictionary_letter_cases(const uint16_t *reading) {
    const word_record *record = (const word_record *)((const char *)reading - offsetof(word_record, reading));
    const uint8_t *cases = word_pack_letter_cases(&active_pack, record);
    if (!cases) cases = word_pack_letter_cases(&sequence_pack, record);
    for (int i = 0; !cases && i < BUILTIN_TABLE_COUNT; i++) {
        const word_record *slots = (const word_record *)builtin_tables[i].address;
        if (builtin_packs[i].records && record >= slots && record < slots + builtin_tables[i].slots) {
            cases = builtin_slot_cases[i][record - slots];
        }
    }
    return cases;
}

const wchar_t *dictionary_half_width_cell(const void *display_cell, int *cell_index) {
    const wchar_t *text = word_pack_half_width_cell(&active_pack, display_cell, cell_index);
    if (!text) text = word_pack_half_width_cell(&sequence_pack, display_cell, cell_index);
    for (int i = 0; !text && i < BUILTIN_TABLE_COUNT; i++) text = word_pack_half_width_cell(&builtin_packs[i], display_cell, cell_index);
    return text;
}

static char *sequence_text_from_tsv(const char *tsv) {
    size_t size = strlen(tsv) + 1;
    char *text = malloc(size), *out = text;
    if (!text) return NULL;
    for (const char *line = tsv; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *tab = memchr(line, '\t', length);
        if (*line != '#' && tab) {
            memcpy(out, tab + 1, length - (size_t)(tab + 1 - line));
            out += length - (size_t)(tab + 1 - line);
            *out++ = '\n';
        }
        line = end ? end + 1 : line + length;
    }
    *out = '\0';
    return text;
}

static void load_word_sequence(void) {
    char path[MAX_PATH], label[MAX_PATH];
    snprintf(path, sizeof path, "%s%s\\%s", dictionary_folder, pack_names[0], SEQUENCE_FILE);
    char *text = word_pack_read_file(path);
    lstrcpynA(label, path, sizeof label);
    if (!text) {
        snprintf(path, sizeof path, "%s%s\\%s", dictionary_folder, pack_names[0], MAIN_TSV);
        char *tsv = word_pack_read_file(path);
        text = tsv ? sequence_text_from_tsv(tsv) : NULL;
        free(tsv);
        snprintf(label, sizeof label, "%s (file order)", path);
    }
    if (!text || !word_pack_load_text(text, label, &sequence_pack) || !sequence_pack.count) {
        free(text);
        word_pack_free(&sequence_pack);
        log_line("dictionary: in-order words need %s or %s in dict\\%s", SEQUENCE_FILE, MAIN_TSV, pack_names[0]);
        return;
    }
    free(text);
    snprintf(sequence_position_path, sizeof sequence_position_path, "%s%s\\%s", dictionary_folder, pack_names[0],
             SEQUENCE_POSITION_FILE);
    FILE *file = fopen(sequence_position_path, "r");
    if (file) {
        if (fscanf(file, "%d", &sequence_position) != 1) sequence_position = 0;
        fclose(file);
    }
    if (sequence_position < 0 || sequence_position >= sequence_pack.count) sequence_position = 0;
    install_hook(PICK_WORD_VA, pick_word_hook, (void **)&original_pick_word, "pick_word");
    log_line("dictionary: words in order from %s (%d entries, starting at %d); words shorter than %d keys stay random",
             label, sequence_pack.count, sequence_position + 1, in_order_min_length);
}

void install_dictionary_hooks(const char *game_folder, const char *ini_path) {
    word_pack_half_width = GetPrivateProfileIntA("text", "half_width", 1, ini_path);
    if (GetPrivateProfileIntA("typing", "case_sensitive", 0, ini_path) &&
        GetPrivateProfileIntA("typing", "case_scoring", 1, ini_path)) {
        word_pack_uppercase_bonus = GetPrivateProfileIntA("typing", "uppercase_points", DEFAULT_UPPERCASE_BONUS, ini_path);
    }
    log_line("dictionary: capital letters add %d to a word's difficulty", word_pack_uppercase_bonus);
    log_line("dictionary: half-width Latin text %s", word_pack_half_width ? "on" : "off");
    word_pack_skip_spaces = !GetPrivateProfileIntA("typing", "type_spaces", 1, ini_path);
    read_word_mutators(ini_path);
    if (word_mutators_active())
        log_line("dictionary: mutators change pack words (order %d, letter case %d)", word_mutator_order, word_mutator_case);
    if (word_pack_skip_spaces) log_line("dictionary: spaces in pack words are not typed (scores unchanged)");
    snprintf(dictionary_folder, sizeof dictionary_folder, "%sdict\\", game_folder);
    read_pack_list(ini_path);
    log_word_sources = GetPrivateProfileIntA("debug", "log_word_sources", 0, ini_path);
    if (log_word_sources) log_line("dictionary: logging every word that does not come from a pack");
    if (pack_count == 0 && !log_word_sources) {
        log_line("dictionary: no packs listed in [dictionary] packs, using word.HAB");
        return;
    }
    for (int i = 0; i < pack_count; i++) log_line("dictionary: pack %d = %s", i + 1, pack_names[i]);
    if (mix_packs && pack_count > 1) log_line("dictionary: mixing the words of all packs that have a list");
    install_hook(LOAD_WORD_ITEM_VA, load_word_item_hook, (void **)&original_load_word_item, "load_word_item");
    install_hook(COUNT_WORD_RECORDS_VA, count_word_records_hook, (void **)&original_count_word_records,
                 "count_word_records");
    install_hook(GET_WORD_RECORD_VA, get_word_record_hook, (void **)&original_get_word_record, "get_word_record");
    if (pack_count && GetPrivateProfileIntA("dictionary", "in_order", 0, ini_path)) {
        in_order_min_length = GetPrivateProfileIntA("dictionary", "in_order_min_length", DEFAULT_IN_ORDER_MIN_LENGTH, ini_path);
        load_word_sequence();
    }
}
