#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "glyph_raster.h"
#include "log.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: render_glyphs text_utf8.txt out.bin [todmod.ini] [--pairs]\n");
        return 2;
    }
    char text[4096] = {0};
    FILE *in = fopen(argv[1], "rb");
    if (!in) return 1;
    fread(text, 1, sizeof text - 1, in);
    fclose(in);
    wchar_t wide[4096];
    int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 4096) - 1;
    int pairs = argc > 4 && strcmp(argv[4], "--pairs") == 0;
    log_open("render_glyphs.log");
    if (argc > 3) glyph_raster_configure(argv[3]);
    FILE *out = fopen(argv[2], "wb");
    for (int i = 0; i < length; i++) {
        uint32_t codepoint = wide[i];
        if (codepoint == '\n' || codepoint == '\r') continue;
        unsigned char glyph[GLYPH_RASTER_BYTES];
        if (pairs) {
            wchar_t second = i + 1 < length && wide[i + 1] != '\n' && wide[i + 1] != '\r' ? wide[++i] : 0;
            glyph_raster_render_pair((wchar_t)codepoint, second, glyph);
        } else {
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < length) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (wide[++i] - 0xDC00);
            }
            glyph_raster_render(codepoint, glyph);
        }
        fwrite(glyph, 1, sizeof glyph, out);
    }
    fclose(out);
    return 0;
}
