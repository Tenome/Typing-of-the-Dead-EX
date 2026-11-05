#include <stdio.h>
#include <stdarg.h>
#include "MinHook.h"
void log_line(const char *format, ...) { va_list a; va_start(a, format); vprintf(format, a); va_end(a); puts(""); }
#define MH_CreateHook(a, b, c) MH_OK
#define MH_EnableHook(a) MH_OK
#define MH_StatusToString(s) "test"
#include "../src/input_hooks.c"
const uint8_t *dictionary_letter_cases(const uint16_t *reading) { return 0; }

static const unsigned rows[4][13] = {
    {0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D},
    {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x2B},
    {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28},
    {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35},
};

int main(void) {
    for (int l = 0; l < (int)(sizeof keyboard_layouts / sizeof keyboard_layouts[0]); l++) {
        printf("%s\n", keyboard_layouts[l].name);
        for (int shifted = 0; shifted < 2; shifted++) {
            for (int r = 0; r < 4; r++) {
                printf("  ");
                for (int k = 0; k < 13 && rows[r][k]; k++) {
                    int c = ascii_from_layout_table(&keyboard_layouts[l], rows[r][k], shifted);
                    putchar(c ? c : '?');
                }
                putchar('\n');
            }
        }
    }
}
