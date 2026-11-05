#include "glyph_tokens.h"

#define TRAIL_BYTES_PER_LEAD 188
#define TOKEN_CAPACITY ((GLYPH_TOKEN_LAST_LEAD - GLYPH_TOKEN_FIRST_LEAD + 1) * TRAIL_BYTES_PER_LEAD - 1)
#define HASH_SLOTS 4096

static uint32_t token_codepoints[TOKEN_CAPACITY];
static int token_count;
static int16_t hash_to_token_index[HASH_SLOTS];

static unsigned token_from_index(int index) {
    unsigned lead = GLYPH_TOKEN_FIRST_LEAD + index / TRAIL_BYTES_PER_LEAD;
    unsigned trail = 0x40 + index % TRAIL_BYTES_PER_LEAD;
    if (trail >= 0x7F) trail++;
    return lead << 8 | trail;
}

static int index_from_token(unsigned token) {
    unsigned lead = token >> 8, trail = token & 0xFF;
    if (lead < GLYPH_TOKEN_FIRST_LEAD || lead > GLYPH_TOKEN_LAST_LEAD) return -1;
    if (trail < 0x40 || trail == 0x7F || trail > 0xFC) return -1;
    int index = (int)(lead - GLYPH_TOKEN_FIRST_LEAD) * TRAIL_BYTES_PER_LEAD + (int)(trail - 0x40) - (trail > 0x7F);
    return index < token_count ? index : -1;
}

int glyph_token_is_token(unsigned code) {
    return index_from_token(code) >= 0;
}

uint32_t glyph_token_codepoint(unsigned token) {
    int index = index_from_token(token);
    return index >= 0 ? token_codepoints[index] : 0;
}

unsigned glyph_token_for_codepoint(uint32_t codepoint) {
    unsigned slot = (codepoint * 2654435761u) % HASH_SLOTS;
    while (hash_to_token_index[slot]) {
        int index = hash_to_token_index[slot] - 1;
        if (token_codepoints[index] == codepoint) return token_from_index(index);
        slot = (slot + 1) % HASH_SLOTS;
    }
    if (token_count == TOKEN_CAPACITY) return 0;
    token_codepoints[token_count] = codepoint;
    hash_to_token_index[slot] = (int16_t)(++token_count);
    return token_from_index(token_count - 1);
}
