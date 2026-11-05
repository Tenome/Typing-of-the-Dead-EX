#include <stdio.h>
#include "pack_coverage.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: coverage_cli dict_folder [pack...]\n");
        return 2;
    }
    char report[4096];
    int complete = pack_coverage_report(argv[1], (const char *const *)argv + 2, argc - 2, report, sizeof report);
    printf("%s\n", report);
    return complete ? 0 : 1;
}
