#pragma once
#include <stdint.h>

#define GLYPH_TOKEN_FIRST_LEAD 0xF0
#define GLYPH_TOKEN_LAST_LEAD 0xF9
#define GLYPH_HALF_WIDTH_CELL 0xF9FC

unsigned glyph_token_for_codepoint(uint32_t codepoint);
uint32_t glyph_token_codepoint(unsigned token);
int glyph_token_is_token(unsigned code);
