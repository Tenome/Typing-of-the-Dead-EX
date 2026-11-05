#include <stdio.h>
#include <stdlib.h>
#include "word_pack.h"
#include "unit_costs.h"

static const char *const builtin_lists[] = {
    "builtin_letters", "builtin_kana", "builtin_short_words", "builtin_drill",
    "builtin_staff_stage", "builtin_staff_kanji", "builtin_staff_kana",
};

static int reading_length(const word_record *record) {
    int length = 0;
    while (length < WORD_MAX_READING_UNITS && record->reading[length]) length++;
    return length;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: builtin_lists_cli words.tsv\n");
        return 2;
    }
    char *tsv = word_pack_read_file(argv[1]);
    if (!tsv) return 2;
    int failures = 0;
    for (int i = 0; i < (int)(sizeof builtin_lists / sizeof builtin_lists[0]); i++) {
        char *lines = word_pack_extract_tsv_list(tsv, builtin_lists[i]);
        word_pack pack = {0};
        if (!lines || !word_pack_load_text(lines, builtin_lists[i], &pack) || pack.count == 0) {
            printf("%s: missing\n", builtin_lists[i]);
            failures++;
            continue;
        }
        printf("%s: %d words\n", builtin_lists[i], pack.count);
        for (int w = 0; w < pack.count; w++) {
            int length = reading_length(&pack.records[w]);
            if (length == 0) failures++;
            printf("  %-3d cost %-3d units", w, word_difficulty(pack.records[w].reading, length));
            for (int u = 0; u < length; u++) printf(" %u", pack.records[w].reading[u]);
            printf("\n");
        }
        word_pack_free(&pack);
        free(lines);
    }
    printf(failures ? "%d problems\n" : "all built-in lists load\n", failures);
    return failures != 0;
}
