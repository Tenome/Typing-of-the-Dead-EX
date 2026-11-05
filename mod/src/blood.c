#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "blood.h"
#include "log.h"

#define MAX_TEXTURE_BANKS 1024
#define FIRST_TEXTURE_BANK 1
#define PIXEL_FORMAT_ARGB1555 0
#define PIXEL_FORMAT_RGB565 1
#define PIXEL_FORMAT_ARGB4444 2
#define RAINBOW_HUE_STEP 47.0f

typedef struct {
    const char *name;
    unsigned char red, green, blue;
    int rainbow;
} blood_colour;

static const blood_colour colours[] = {
    {"purple", 150, 40, 220, 0}, {"gold", 255, 200, 40, 0},  {"blue", 40, 100, 255, 0},  {"grey", 170, 170, 170, 0},
    {"black", 45, 40, 40, 0},    {"white", 245, 245, 245, 0}, {"pink", 255, 90, 190, 0}, {"orange", 255, 120, 20, 0},
    {"cyan", 30, 220, 230, 0},   {"yellow", 250, 240, 40, 0}, {"rainbow", 0, 0, 0, 1},
    {"paintball", 245, 245, 245, 0},
};
#define COLOUR_COUNT ((int)(sizeof colours / sizeof colours[0]))

static const blood_colour *tint;
static char recoloured_bank[64];

static int bank_named(const char *const *names, const char *name) {
    for (int i = FIRST_TEXTURE_BANK; i < MAX_TEXTURE_BANKS; i++) {
        if (IsBadReadPtr(&names[i], sizeof names[i]) || !names[i] || IsBadStringPtrA(names[i], 64)) return -1;
        if (_stricmp(names[i], name) == 0) return i;
    }
    return -1;
}

static void hue_colour(float hue, unsigned char *red, unsigned char *green, unsigned char *blue) {
    float h = fmodf(hue, 360.0f) / 60.0f, x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    if (h < 1) r = 1, g = x;
    else if (h < 2) r = x, g = 1;
    else if (h < 3) g = 1, b = x;
    else if (h < 4) g = x, b = 1;
    else if (h < 5) r = x, b = 1;
    else r = 1, b = x;
    *red = (unsigned char)(r * 255), *green = (unsigned char)(g * 255), *blue = (unsigned char)(b * 255);
}

static void unpack(uint16_t pixel, int format, unsigned *r, unsigned *g, unsigned *b) {
    if (format == PIXEL_FORMAT_RGB565) {
        *r = (pixel >> 11 & 31) * 255 / 31, *g = (pixel >> 5 & 63) * 255 / 63, *b = (pixel & 31) * 255 / 31;
    } else if (format == PIXEL_FORMAT_ARGB4444) {
        *r = (pixel >> 8 & 15) * 17, *g = (pixel >> 4 & 15) * 17, *b = (pixel & 15) * 17;
    } else {
        *r = (pixel >> 10 & 31) * 255 / 31, *g = (pixel >> 5 & 31) * 255 / 31, *b = (pixel & 31) * 255 / 31;
    }
}

static uint16_t pack(uint16_t original, int format, unsigned r, unsigned g, unsigned b) {
    if (format == PIXEL_FORMAT_RGB565) return (uint16_t)((r * 31 / 255) << 11 | (g * 63 / 255) << 5 | b * 31 / 255);
    if (format == PIXEL_FORMAT_ARGB4444) return (uint16_t)((original & 0xF000) | (r / 17) << 8 | (g / 17) << 4 | b / 17);
    return (uint16_t)((original & 0x8000) | (r * 31 / 255) << 10 | (g * 31 / 255) << 5 | b * 31 / 255);
}

static unsigned tinted(unsigned shade, unsigned highlight, unsigned target) {
    unsigned value = shade * target / 255 + highlight;
    return value > 255 ? 255 : value;
}

uint16_t *blood_recolour(const char *bank_name, int index, int width, int height, int pixel_format, const void *pixels) {
    if (!tint || !bank_name || _stricmp(bank_name, recoloured_bank) != 0 || pixel_format > PIXEL_FORMAT_ARGB4444) return NULL;
    unsigned char red = tint->red, green = tint->green, blue = tint->blue;
    if (tint->rainbow) hue_colour(index * RAINBOW_HUE_STEP, &red, &green, &blue);
    size_t count = (size_t)width * height;
    uint16_t *out = malloc(count * sizeof *out);
    if (!out) return NULL;
    const uint16_t *in = pixels;
    for (size_t i = 0; i < count; i++) {
        unsigned r, g, b;
        unpack(in[i], pixel_format, &r, &g, &b);
        unsigned highlight = g < b ? g : b;
        unsigned shade = r > highlight ? r - highlight : 0;
        out[i] = pack(in[i], pixel_format, tinted(shade, highlight, red), tinted(shade, highlight, green),
                      tinted(shade, highlight, blue));
    }
    return out;
}

int blood_is_paintball(void) {
    return tint && strcmp(tint->name, "paintball") == 0;
}

int blood_needs_recolouring(void) {
    return tint != NULL;
}

void install_blood_colour(const char *ini_path) {
    char colour[16] = "";
    GetPrivateProfileStringA("display", "blood", "green", colour, sizeof colour, ini_path);
    if (_stricmp(colour, "green") == 0 || !colour[0]) return;
    for (int i = 0; i < COLOUR_COUNT; i++) {
        if (_stricmp(colour, colours[i].name) == 0) tint = &colours[i];
    }
    if (!tint && _stricmp(colour, "red") != 0) {
        log_line("blood: unknown colour '%s', blood stays green", colour);
        return;
    }
    HMODULE table = GetModuleHandleA("Table.dll");
    const char **names = table ? (const char **)GetProcAddress(table, "TexFileNameTbl") : NULL;
    int green = names ? bank_named(names, "scr_blood_green.bin") : -1;
    int red = names ? bank_named(names, "scr_blood_red.bin") : -1;
    if (green < 0 || red < 0) {
        tint = NULL;
        log_line("blood: blood texture banks not found, blood stays green");
        return;
    }
    DWORD protection;
    if (!VirtualProtect((void *)&names[green], sizeof names[green], PAGE_READWRITE, &protection)) return;
    names[green] = names[red];
    VirtualProtect((void *)&names[green], sizeof names[green], protection, &protection);
    lstrcpynA(recoloured_bank, "scr_blood_red", sizeof recoloured_bank);
    log_line("blood: %s (texture bank %d reads %s%s)", colour, green, names[red], tint ? ", recoloured as it loads" : "");
}
