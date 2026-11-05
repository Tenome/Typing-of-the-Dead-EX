#pragma once
#include <stdint.h>
#include <wchar.h>

#define GLYPH_RASTER_SIZE 24
#define GLYPH_RASTER_BYTES (GLYPH_RASTER_SIZE * GLYPH_RASTER_SIZE / 4)

void glyph_raster_configure(const char *ini_path);
void glyph_raster_render(uint32_t codepoint, unsigned char *out_2bpp);
void glyph_raster_render_pair(wchar_t first, wchar_t second, unsigned char *out_2bpp);
