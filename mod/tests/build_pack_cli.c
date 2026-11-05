#include <stdio.h>
#include <stdlib.h>
#include "pack_builder.h"
#include "log.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: build_pack_cli word_list.txt pack_folder [seed]\n");
        return 2;
    }
    log_open("build_pack_cli.log");
    pack_build_report report;
    char error[512];
    unsigned seed = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 0;
    if (!build_pack_from_word_list(argv[1], argv[2], seed, &report, error, sizeof error)) {
        printf("failed: %s\n", error);
        return 1;
    }
    printf("read %d, usable %d, untypable %d, too long %d, duplicates %d; lists written %d, kept %d, slots %d\n",
           report.words_read, report.words_usable, report.untypable, report.too_long, report.duplicates,
           report.lists_written, report.lists_kept, report.slots_filled);
    return 0;
}
