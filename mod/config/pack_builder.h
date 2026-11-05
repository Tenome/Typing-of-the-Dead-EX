#pragma once
#include <stddef.h>

typedef struct {
    int words_read;
    int words_usable;
    int untypable;
    int too_long;
    int duplicates;
    int lists_written;
    int lists_kept;
    int slots_filled;
} pack_build_report;

int build_pack_from_word_list(const char *word_list_path, const char *pack_folder, unsigned seed,
                              pack_build_report *report, char *error, size_t error_size);
