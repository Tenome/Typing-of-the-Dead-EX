#include <windows.h>
#include <math.h>
#include <string.h>
#include "glyph_raster.h"
#include "log.h"

#define CANVAS_WIDTH 96
#define CANVAS_HEIGHT 64
#define CANVAS_BASELINE 44
#define CANVAS_PEN_X 16
#define NATIVE_BASELINE_ROW 22
#define NATIVE_CAP_HEIGHT 20
#define INK_THRESHOLD 8
#define LEVELS_PER_PIXEL 4
#define PIXELS_PER_BYTE 4
#define PROBE_EM 100

static char font_face[LF_FACESIZE] = "Arial";
static char half_width_face[LF_FACESIZE] = "Consolas";
static int font_weight = FW_NORMAL;
static int cap_height = NATIVE_CAP_HEIGHT;

static HDC canvas_dc;
static uint32_t *canvas_pixels;
static HFONT full_width_font, half_width_font;
static float half_width_pair_advance;

typedef struct {
    int left, right, top, bottom;
} ink_box;

void glyph_raster_configure(const char *ini_path) {
    GetPrivateProfileStringA("text", "font", font_face, font_face, sizeof font_face, ini_path);
    font_weight = GetPrivateProfileIntA("text", "weight", font_weight, ini_path);
    cap_height = GetPrivateProfileIntA("text", "cap_height", cap_height, ini_path);
    GetPrivateProfileStringA("text", "half_width_font", half_width_face, half_width_face, sizeof half_width_face,
                             ini_path);
    log_line("glyphs: font '%s', weight %d, capital height %d px", font_face, font_weight, cap_height);
}

static HFONT create_font(const char *face, int em) {
    return CreateFontA(-em, 0, 0, 0, font_weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}

static int em_for_cap_height(const char *face) {
    HFONT probe = create_font(face, PROBE_EM);
    HGDIOBJ previous = SelectObject(canvas_dc, probe);
    GLYPHMETRICS metrics;
    MAT2 identity = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    DWORD result = GetGlyphOutlineW(canvas_dc, L'H', GGO_METRICS, &metrics, 0, NULL, &identity);
    SelectObject(canvas_dc, previous);
    DeleteObject(probe);
    if (result == GDI_ERROR || metrics.gmBlackBoxY == 0) return cap_height * 10 / 7;
    return (int)lroundf((float)PROBE_EM * (float)cap_height / (float)metrics.gmBlackBoxY);
}

static int create_canvas(void) {
    if (canvas_dc) return canvas_pixels != NULL;
    canvas_dc = CreateCompatibleDC(NULL);
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = CANVAS_WIDTH;
    info.bmiHeader.biHeight = -CANVAS_HEIGHT;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HBITMAP bitmap = CreateDIBSection(canvas_dc, &info, DIB_RGB_COLORS, (void **)&canvas_pixels, NULL, 0);
    int em = em_for_cap_height(font_face);
    full_width_font = create_font(font_face, em);
    int half_width_em = em_for_cap_height(half_width_face);
    half_width_font = create_font(half_width_face, half_width_em);
    if (!bitmap || !full_width_font || !half_width_font) {
        log_line("glyphs: cannot create GDI canvas or font");
        canvas_pixels = NULL;
        return 0;
    }
    SelectObject(canvas_dc, bitmap);
    SelectObject(canvas_dc, half_width_font);
    SIZE pair;
    GetTextExtentPoint32W(canvas_dc, L"MM", 2, &pair);
    half_width_pair_advance = (float)pair.cx;
    SelectObject(canvas_dc, full_width_font);
    SetTextColor(canvas_dc, RGB(255, 255, 255));
    SetBkMode(canvas_dc, TRANSPARENT);
    SetTextAlign(canvas_dc, TA_BASELINE | TA_LEFT);
    log_line("glyphs: %d px em gives %d px capitals; half-width '%s' %d px em, %.0f px per pair", em, cap_height,
             half_width_face, half_width_em, half_width_pair_advance);
    return 1;
}

static int codepoint_to_utf16(uint32_t codepoint, wchar_t *out) {
    if (codepoint < 0x10000) {
        out[0] = (wchar_t)codepoint;
        return 1;
    }
    codepoint -= 0x10000;
    out[0] = (wchar_t)(0xD800 + (codepoint >> 10));
    out[1] = (wchar_t)(0xDC00 + (codepoint & 0x3FF));
    return 2;
}

static int coverage_at(int x, int y) {
    return canvas_pixels[y * CANVAS_WIDTH + x] & 0xFF;
}

static int draw_text(const wchar_t *text, int length, ink_box *ink) {
    memset(canvas_pixels, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof *canvas_pixels);
    TextOutW(canvas_dc, CANVAS_PEN_X, CANVAS_BASELINE, text, length);
    GdiFlush();
    ink->left = CANVAS_WIDTH, ink->right = -1, ink->top = CANVAS_HEIGHT, ink->bottom = -1;
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            if (coverage_at(x, y) < INK_THRESHOLD) continue;
            if (x < ink->left) ink->left = x;
            if (x > ink->right) ink->right = x;
            if (y < ink->top) ink->top = y;
            if (y > ink->bottom) ink->bottom = y;
        }
    }
    return ink->right >= 0;
}

static int draw_codepoint(uint32_t codepoint, ink_box *ink) {
    wchar_t text[2];
    int length = codepoint_to_utf16(codepoint, text);
    return draw_text(text, length, ink);
}

static float area_coverage(float x0, float x1, float y0, float y1) {
    float total = 0.0f, area = 0.0f;
    for (int y = (int)floorf(y0); y < (int)ceilf(y1); y++) {
        float height = fminf(y1, (float)y + 1.0f) - fmaxf(y0, (float)y);
        if (height <= 0.0f || y < 0 || y >= CANVAS_HEIGHT) continue;
        for (int x = (int)floorf(x0); x < (int)ceilf(x1); x++) {
            float width = fminf(x1, (float)x + 1.0f) - fmaxf(x0, (float)x);
            if (width <= 0.0f || x < 0 || x >= CANVAS_WIDTH) continue;
            total += (float)coverage_at(x, y) * width * height;
            area += width * height;
        }
    }
    return area > 0.0f ? total / area : 0.0f;
}

static float source_row(float output_row, const ink_box *ink) {
    float above = (float)(CANVAS_BASELINE - ink->top);
    float below = (float)(ink->bottom + 1 - CANVAS_BASELINE);
    float room_above = (float)NATIVE_BASELINE_ROW;
    float room_below = (float)(GLYPH_RASTER_SIZE - NATIVE_BASELINE_ROW);
    float offset = output_row - (float)NATIVE_BASELINE_ROW;
    if (offset < 0.0f) return (float)CANVAS_BASELINE + offset * (above > room_above ? above / room_above : 1.0f);
    return (float)CANVAS_BASELINE + offset * (below > room_below ? below / room_below : 1.0f);
}

static void resample_into_cell(const ink_box *ink, float x_origin, float x_scale, unsigned char *out_2bpp) {
    for (int y = 0; y < GLYPH_RASTER_SIZE; y++) {
        float y0 = source_row((float)y, ink), y1 = source_row((float)y + 1.0f, ink);
        for (int x = 0; x < GLYPH_RASTER_SIZE; x++) {
            float x0 = x_origin + (float)x * x_scale, x1 = x0 + x_scale;
            int level = ((int)area_coverage(x0, x1, y0, y1) + 42) * (LEVELS_PER_PIXEL - 1) / 255;
            if (level > LEVELS_PER_PIXEL - 1) level = LEVELS_PER_PIXEL - 1;
            int bit_shift = 6 - 2 * (x % PIXELS_PER_BYTE);
            out_2bpp[(y * GLYPH_RASTER_SIZE + x) / PIXELS_PER_BYTE] |= (unsigned char)(level << bit_shift);
        }
    }
}

void glyph_raster_render(uint32_t codepoint, unsigned char *out_2bpp) {
    memset(out_2bpp, 0, GLYPH_RASTER_BYTES);
    ink_box ink;
    if (!create_canvas() || !draw_codepoint(codepoint, &ink)) return;
    float ink_width = (float)(ink.right + 1 - ink.left);
    float x_scale = ink_width > GLYPH_RASTER_SIZE ? ink_width / GLYPH_RASTER_SIZE : 1.0f;
    float x_origin = ink_width > GLYPH_RASTER_SIZE ? (float)ink.left
                                                     : (float)ink.left - floorf((GLYPH_RASTER_SIZE - ink_width) / 2.0f);
    resample_into_cell(&ink, x_origin, x_scale, out_2bpp);
}

void glyph_raster_render_pair(wchar_t first, wchar_t second, unsigned char *out_2bpp) {
    memset(out_2bpp, 0, GLYPH_RASTER_BYTES);
    if (!create_canvas()) return;
    wchar_t pair[2] = {first, second ? second : L' '};
    ink_box ink;
    SelectObject(canvas_dc, half_width_font);
    int has_ink = draw_text(pair, 2, &ink);
    SelectObject(canvas_dc, full_width_font);
    if (!has_ink) return;
    resample_into_cell(&ink, (float)CANVAS_PEN_X, half_width_pair_advance / GLYPH_RASTER_SIZE, out_2bpp);
}
