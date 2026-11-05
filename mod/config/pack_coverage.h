#pragma once
#include <stddef.h>

int pack_coverage_report(const char *dict_folder, const char *const *packs, int pack_count, char *out,
                         size_t out_size);
