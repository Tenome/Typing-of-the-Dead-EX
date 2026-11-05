#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "word_pack.h"
#include "log.h"

static char *read_bin(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = calloc(1, size + 1);
    fread(data, 1, size, file);
    fclose(file);
    return data;
}

static char *tsv_text;

static int load_pack_list(const char *pack_dir, const char *item, word_pack *pack) {
    if (tsv_text) {
        char *lines = word_pack_extract_tsv_list(tsv_text, item);
        int loaded = lines && word_pack_load_text(lines, item, pack);
        free(lines);
        return loaded;
    }
    char pack_path[512];
    snprintf(pack_path, sizeof pack_path, "%s/%s.txt", pack_dir, item);
    return word_pack_load(pack_path, pack);
}

static int verify_item(const char *bin_dir, const char *pack_dir, const char *item) {
    char bin_path[512];
    snprintf(bin_path, sizeof bin_path, "%s/%s.bin", bin_dir, item);
    char *bin = read_bin(bin_path);
    word_pack pack = {0};
    if (!bin || !load_pack_list(pack_dir, item, &pack)) {
        printf("%s: cannot read\n", item);
        free(bin);
        return 0;
    }
    const word_record *hab = (const word_record *)bin;
    int hab_count = 0;
    while (hab[hab_count].group != WORD_GROUP_TERMINATOR && hab[hab_count].reading[0]) hab_count++;
    int mismatches = hab_count != pack.count;
    if (mismatches) printf("%s: HAB %d words, pack %d\n", item, hab_count, pack.count);
    int compared = hab_count < pack.count ? hab_count : pack.count;
    for (int i = 0; i < compared; i++) {
        const char *hab_display = bin + (size_t)hab[i].display;
        const word_record *mine = &pack.records[i];
        if (hab[i].group == mine->group && !memcmp(hab[i].reading, mine->reading, sizeof mine->reading) &&
            !strcmp(hab_display, mine->display)) {
            continue;
        }
        if (mismatches++ < 5) {
            printf("%s[%d]: group %u/%u reading", item, i, hab[i].group, mine->group);
            for (int u = 0; u < 8; u++) printf(" %u/%u", hab[i].reading[u], mine->reading[u]);
            printf(" display %s\n", strcmp(hab_display, mine->display) ? "differs" : "same");
        }
    }
    word_pack_free(&pack);
    free(bin);
    return mismatches == 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: verify_pack bin_dir pack_dir item...\n");
        return 2;
    }
    log_open("verify_pack.log");
    size_t pack_length = strlen(argv[2]);
    if (pack_length > 4 && _stricmp(argv[2] + pack_length - 4, ".tsv") == 0) tsv_text = word_pack_read_file(argv[2]);
    int passed = 0;
    for (int i = 3; i < argc; i++) passed += verify_item(argv[1], argv[2], argv[i]);
    printf("%d of %d items identical\n", passed, argc - 3);
    return passed == argc - 3 ? 0 : 1;
}
