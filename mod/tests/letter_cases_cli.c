#include <stdio.h>
#include <stdlib.h>
#include "word_pack.h"

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    word_pack_uppercase_bonus = argc > 2 ? atoi(argv[2]) : 0;
    char *text = word_pack_read_file(argv[1]);
    word_pack pack = {0};
    if (!text || !word_pack_load_text(text, argv[1], &pack)) return 2;
    for (int w = 0; w < pack.count; w++) {
        const uint8_t *cases = word_pack_letter_cases(&pack, &pack.records[w]);
        printf("group %-3u cases ", pack.records[w].group);
        for (int u = 0; pack.records[w].reading[u]; u++) putchar(".UL"[cases[u]]);
        printf("  (%d capitals)\n", word_pack_uppercase_count(cases));
    }
    return 0;
}
