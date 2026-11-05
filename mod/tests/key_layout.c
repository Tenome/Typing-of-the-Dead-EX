#include <stdio.h>
#include <stdarg.h>
#include "MinHook.h"
void log_line(const char *format, ...) { va_list a; va_start(a, format); vprintf(format, a); va_end(a); puts(""); }
#define MH_CreateHook(a, b, c) MH_OK
#define MH_EnableHook(a) MH_OK
#define MH_StatusToString(s) "test"
#include "../src/input_hooks.c"

int main(int argc, char **argv) {
    FILE *file = fopen(argv[1], "rb");
    static unsigned char exe[0x200000];
    fread(exe, 1, sizeof exe, file);
    unsigned plain_offset = strtoul(argv[2], 0, 16), shift_offset = strtoul(argv[3], 0, 16);
    for (int i = 0; i < (int)(sizeof usage_scancodes / sizeof usage_scancodes[0]); i++) {
        unsigned char usage = usage_scancodes[i].usage;
        int plain = ascii_from_layout(usage_scancodes[i].scancode, 0), shifted = ascii_from_layout(usage_scancodes[i].scancode, 1);
        unsigned char jp_plain = exe[plain_offset + usage], jp_shift = exe[shift_offset + usage];
        printf("%02X  game %c %c   layout %c %c\n", usage, jp_plain >= 32 && jp_plain < 127 ? jp_plain : '.',
               jp_shift >= 32 && jp_shift < 127 ? jp_shift : '.', plain ? plain : '.', shifted ? shifted : '.');
    }
}
