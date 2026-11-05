#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack_coverage.h"
#include "pack_template.h"

#define BANK_COUNT 2
#define LIST_NAME_LENGTH 32
#define FAMILY_LENGTH 4
#define MAX_FAMILIES 32
#define EXAMPLES_PER_FAMILY 3

typedef struct {
    char (*names)[LIST_NAME_LENGTH];
    int count;
} name_set;

static const char *const builtin_lists[] = {
    "builtin_letters", "builtin_kana", "builtin_short_words", "builtin_drill",
    "builtin_staff_stage", "builtin_staff_kanji", "builtin_staff_kana",
};
#define BUILTIN_LIST_COUNT ((int)(sizeof builtin_lists / sizeof builtin_lists[0]))
#define MAIN_BANK 0

static const char *const bank_files[BANK_COUNT] = {"words.tsv", "kids.tsv"};
static const char *const bank_labels[BANK_COUNT] = {"Main game", "Kids' Mode"};

static int compare_names(const void *a, const void *b) {
    return _stricmp((const char *)a, (const char *)b);
}

static name_set read_tsv_list_names(const char *path) {
    name_set set = {NULL, 0};
    FILE *file = fopen(path, "rb");
    if (!file) return set;
    int capacity = 0;
    char line[512];
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *extension = strrchr(line, '.');
        if (extension && _stricmp(extension, ".bin") == 0) *extension = '\0';
        if (set.count && _stricmp(set.names[set.count - 1], line) == 0) continue;
        if (set.count == capacity) {
            capacity = capacity ? capacity * 2 : 256;
            set.names = realloc(set.names, capacity * sizeof *set.names);
        }
        lstrcpynA(set.names[set.count++], line, LIST_NAME_LENGTH);
    }
    fclose(file);
    if (set.count) qsort(set.names, set.count, sizeof *set.names, compare_names);
    return set;
}

static int set_contains(const name_set *set, const char *name) {
    return set->count && bsearch(name, set->names, set->count, sizeof *set->names, compare_names) != NULL;
}

static int list_file_exists(const char *dict_folder, const char *pack, int kids, const char *name) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s\\%s%s.txt", dict_folder, pack, kids ? "kids\\" : "", name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

typedef struct {
    char family[FAMILY_LENGTH + 1];
    int missing;
    char examples[EXAMPLES_PER_FAMILY][LIST_NAME_LENGTH];
} family_gap;

static void note_gap(family_gap *gaps, int *gap_count, const char *name) {
    int i = 0;
    while (i < *gap_count && strncmp(gaps[i].family, name, FAMILY_LENGTH) != 0) i++;
    if (i == *gap_count) {
        if (*gap_count == MAX_FAMILIES) return;
        memset(&gaps[i], 0, sizeof gaps[i]);
        lstrcpynA(gaps[i].family, name, FAMILY_LENGTH + 1);
        (*gap_count)++;
    }
    if (gaps[i].missing < EXAMPLES_PER_FAMILY) lstrcpynA(gaps[i].examples[gaps[i].missing], name, LIST_NAME_LENGTH);
    gaps[i].missing++;
}

static size_t append(char *out, size_t out_size, size_t used, const char *format, ...) {
    if (used >= out_size) return used;
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(out + used, out_size - used, format, arguments);
    va_end(arguments);
    return written < 0 ? used : used + (size_t)written;
}

int pack_coverage_report(const char *dict_folder, const char *const *packs, int pack_count, char *out,
                         size_t out_size) {
    name_set *sets = calloc((size_t)(pack_count ? pack_count : 1) * BANK_COUNT, sizeof *sets);
    for (int p = 0; p < pack_count; p++) {
        for (int bank = 0; bank < BANK_COUNT; bank++) {
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%s\\%s\\%s", dict_folder, packs[p], bank_files[bank]);
            sets[p * BANK_COUNT + bank] = read_tsv_list_names(path);
        }
    }
    int fully_covered = 1;
    size_t used = 0;
    for (int bank = 0; bank < BANK_COUNT; bank++) {
        family_gap gaps[MAX_FAMILIES];
        int gap_count = 0, total = 0, covered = 0;
        for (int i = 0; i < template_list_count; i++) {
            const template_list *list = &template_lists[i];
            if (list->kids != bank) continue;
            total++;
            int found = 0;
            for (int p = 0; p < pack_count && !found; p++) {
                found = list_file_exists(dict_folder, packs[p], bank, list->name) ||
                        set_contains(&sets[p * BANK_COUNT + bank], list->name);
            }
            if (found) covered++;
            else note_gap(gaps, &gap_count, list->name);
        }
        used = append(out, out_size, used, "%s: %d of %d word lists come from your packs.\n", bank_labels[bank],
                      covered, total);
        for (int g = 0; g < gap_count; g++) {
            used = append(out, out_size, used, "    %s: %d %s the game's own words (e.g. %s%s%s%s%s)\n",
                          gaps[g].family, gaps[g].missing, gaps[g].missing == 1 ? "list uses" : "lists use",
                          gaps[g].examples[0],
                          gaps[g].missing > 1 ? ", " : "", gaps[g].missing > 1 ? gaps[g].examples[1] : "",
                          gaps[g].missing > 2 ? ", " : "", gaps[g].missing > 2 ? gaps[g].examples[2] : "");
        }
        if (covered < total) fully_covered = 0;
        used = append(out, out_size, used, "\n");
    }
    int builtin_covered = 0;
    char builtin_gaps[512] = "";
    for (int i = 0; i < BUILTIN_LIST_COUNT; i++) {
        int found = 0;
        for (int p = 0; p < pack_count && !found; p++) found = set_contains(&sets[p * BANK_COUNT + MAIN_BANK], builtin_lists[i]);
        if (found) {
            builtin_covered++;
        } else {
            if (builtin_gaps[0]) strncat(builtin_gaps, ", ", sizeof builtin_gaps - strlen(builtin_gaps) - 1);
            strncat(builtin_gaps, builtin_lists[i], sizeof builtin_gaps - strlen(builtin_gaps) - 1);
        }
    }
    used = append(out, out_size, used, "Built into Tod.exe (drills, single letters, staff names): %d of %d lists come "
                  "from your packs (%s).\n", builtin_covered, BUILTIN_LIST_COUNT, bank_files[MAIN_BANK]);
    if (builtin_gaps[0]) used = append(out, out_size, used, "    Using the game's own words: %s\n", builtin_gaps);
    if (builtin_covered < BUILTIN_LIST_COUNT) fully_covered = 0;
    used = append(out, out_size, used, "\n");
    used = append(out, out_size, used, fully_covered
        ? "Every word list comes from your packs: no original (Japanese) words will appear."
        : "Lists not covered use the game's own words (Japanese unless word.HAB was replaced). Add a pack that "
          "covers them, such as one made with english_to_pack.py, lower in \"In use\".");
    for (int i = 0; i < pack_count * BANK_COUNT; i++) free(sets[i].names);
    free(sets);
    return fully_covered;
}
