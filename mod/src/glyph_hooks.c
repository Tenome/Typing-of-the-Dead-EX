#include <windows.h>
#include <string.h>
#include "glyph_hooks.h"
#include "glyph_raster.h"
#include "glyph_tokens.h"
#include "word_pack.h"
#include "dictionary_hooks.h"
#include "log.h"
#include "MinHook.h"

#define RASTERISE_GLYPH_VA 0x00455ff0
#define RASTERISE_CELL_VA 0x00456140
#define FONT_BUFFER (*(unsigned char **)0x00757940)
#define FIRST_FONT_CODE 0x8140
#define LAST_FONT_CODE 0xEAA4
#define FONT_GLYPH_HEADER_BYTES 16
#define FONT_OFFSET_UNIT 32
#define DRAW_WORD_BOX_VA 0x004c84f0
#define DRAW_READING_JAPANESE_VA 0x004c8370
#define DRAW_READING_ENGLISH_VA 0x004c83d0
#define WORD_LETTER_COUNT 0x98
#define WORD_DISPLAY 0x50
#define GAME_LANGUAGE (*(volatile short *)0x00a1b6b2)
#define LANGUAGE_JAPANESE 1

typedef int(__cdecl *rasterise_glyph_fn)(const unsigned char *sjis, int texture_id, int mode);

static rasterise_glyph_fn original_rasterise_glyph;
static rasterise_glyph_fn original_rasterise_cell;
typedef void(__cdecl *draw_word_box_fn)(float *layout, char *word);
static draw_word_box_fn original_draw_word_box;
static const wchar_t *pending_half_width_text;
static int pending_half_width_cell;
static const unsigned short *kanji_offset;
static const unsigned char carrier_code[] = {FIRST_FONT_CODE >> 8, FIRST_FONT_CODE & 0xFF, 0};

static int font_has_glyph(unsigned code) {
    return code == FIRST_FONT_CODE || kanji_offset[code - FIRST_FONT_CODE] != 0;
}

static unsigned char *carrier_glyph_data(void) {
    unsigned char *font = FONT_BUFFER;
    if (!font) return NULL;
    return font + kanji_offset[0] * FONT_OFFSET_UNIT + FONT_GLYPH_HEADER_BYTES;
}

static void render_requested_glyph(unsigned code, uint32_t codepoint, unsigned char *carrier) {
    if (code != GLYPH_HALF_WIDTH_CELL) {
        glyph_raster_render(codepoint, carrier);
        return;
    }
    memset(carrier, 0, GLYPH_RASTER_BYTES);
    if (!pending_half_width_text) return;
    int first = pending_half_width_cell * 2;
    size_t length = wcslen(pending_half_width_text);
    if ((size_t)first >= length) return;
    static int reported;
    if (!reported++) log_line("glyphs: first half-width cell drawn (cell %d of a %u-character word)", pending_half_width_cell, (unsigned)length);
    glyph_raster_render_pair(pending_half_width_text[first], (size_t)first + 1 < length ? pending_half_width_text[first + 1] : 0,
                             carrier);
}

static int __cdecl rasterise_glyph_hook(const unsigned char *sjis, int texture_id, int mode) {
    unsigned code = sjis ? (unsigned)sjis[0] << 8 | sjis[1] : 0;
    uint32_t codepoint = glyph_token_codepoint(code);
    unsigned char *carrier = codepoint || code == GLYPH_HALF_WIDTH_CELL ? carrier_glyph_data() : NULL;
    if (!carrier) return original_rasterise_glyph(sjis, texture_id, mode);
    unsigned char saved[GLYPH_RASTER_BYTES];
    memcpy(saved, carrier, sizeof saved);
    render_requested_glyph(code, codepoint, carrier);
    int result = original_rasterise_glyph(carrier_code, texture_id, mode);
    memcpy(carrier, saved, sizeof saved);
    return result;
}

static int __cdecl rasterise_cell_hook(const unsigned char *cell, int texture_id, int mode) {
    pending_half_width_text = cell ? dictionary_half_width_cell(cell, &pending_half_width_cell) : NULL;
    int result = original_rasterise_cell(cell, texture_id, mode);
    pending_half_width_text = NULL;
    return result;
}

#define CJK_FIRST_CODEPOINT 0x2E80

static int is_sjis_lead(unsigned char byte) {
    return (byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC);
}

static int is_wide_script_code(unsigned code) {
    if (code == GLYPH_HALF_WIDTH_CELL) return 1;
    if (glyph_token_is_token(code)) return glyph_token_codepoint(code) >= CJK_FIRST_CODEPOINT;
    unsigned lead = code >> 8, trail = code & 0xFF;
    return (lead == 0x82 && trail >= 0x9F) || lead == 0x83 || lead == 0x84 || lead >= 0x88;
}

static int needs_japanese_layout(const unsigned char *display) {
    if (!display) return 0;
    for (const unsigned char *c = display; *c;) {
        if (!is_sjis_lead(*c) || !c[1]) {
            c++;
            continue;
        }
        if (is_wide_script_code((unsigned)c[0] << 8 | c[1])) return 1;
        c += 2;
    }
    return 0;
}

static void __cdecl draw_word_box_hook(float *layout, char *word) {
    short language = GAME_LANGUAGE;
    int narrow = language != LANGUAGE_JAPANESE && word && needs_japanese_layout(*(unsigned char **)(word + WORD_DISPLAY));
    if (narrow) GAME_LANGUAGE = LANGUAGE_JAPANESE;
    original_draw_word_box(layout, word);
    if (narrow) GAME_LANGUAGE = language;
}

typedef void(__cdecl *draw_reading_fn)(float *layout, char *word);
static draw_reading_fn original_draw_reading_japanese, original_draw_reading_english;

static int reading_line_hidden(const char *word) {
    return word && *(const unsigned char *)(word + WORD_LETTER_COUNT) > 1 && *(const char *const *)(word + WORD_DISPLAY);
}

static void __cdecl draw_reading_japanese_hook(float *layout, char *word) {
    if (!reading_line_hidden(word)) original_draw_reading_japanese(layout, word);
}

static void __cdecl draw_reading_english_hook(float *layout, char *word) {
    if (!reading_line_hidden(word)) original_draw_reading_english(layout, word);
}

static void install_hook(DWORD address, void *hook, void **original, const char *name) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, original);
    if (status == MH_OK) status = MH_EnableHook((void *)address);
    log_line("hook %s at %08lX: %s", name, address, MH_StatusToString(status));
}

void install_glyph_hooks(const char *ini_path) {
    kanji_offset = (const unsigned short *)GetProcAddress(GetModuleHandleA("Table.dll"), "kanji_offset");
    if (!kanji_offset) {
        log_line("glyphs: kanji_offset not found in Table.dll, Unicode glyphs disabled");
        return;
    }
    word_pack_font_has_glyph = font_has_glyph;
    glyph_raster_configure(ini_path);
    install_hook(RASTERISE_GLYPH_VA, rasterise_glyph_hook, (void **)&original_rasterise_glyph, "rasterise_glyph");
    install_hook(RASTERISE_CELL_VA, rasterise_cell_hook, (void **)&original_rasterise_cell, "rasterise_cell");
    install_hook(DRAW_WORD_BOX_VA, draw_word_box_hook, (void **)&original_draw_word_box, "draw_word_box");
    if (GetPrivateProfileIntA("typing", "hide_reading", 0, ini_path)) {
        install_hook(DRAW_READING_JAPANESE_VA, draw_reading_japanese_hook, (void **)&original_draw_reading_japanese,
                     "draw_reading (Japanese layout, hidden)");
        install_hook(DRAW_READING_ENGLISH_VA, draw_reading_english_hook, (void **)&original_draw_reading_english,
                     "draw_reading (English layout, hidden)");
    }
}
