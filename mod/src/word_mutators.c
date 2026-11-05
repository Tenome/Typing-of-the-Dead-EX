#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "word_mutators.h"

int word_mutator_order;
int word_mutator_case;

void read_word_mutators(const char *ini_path) {
    char text[32];
    GetPrivateProfileStringA("mutators", "words", "normal", text, sizeof text, ini_path);
    word_mutator_order = !lstrcmpiA(text, "reversed")    ? WORD_ORDER_REVERSED
                         : !lstrcmpiA(text, "scrambled") ? WORD_ORDER_SCRAMBLED
                                                         : WORD_ORDER_NORMAL;
    GetPrivateProfileStringA("mutators", "letter_case", "normal", text, sizeof text, ini_path);
    word_mutator_case = !lstrcmpiA(text, "upper")    ? LETTER_CASE_UPPER
                        : !lstrcmpiA(text, "lower")  ? LETTER_CASE_LOWER
                        : !lstrcmpiA(text, "random") ? LETTER_CASE_RANDOM
                                                     : LETTER_CASE_NORMAL;
}

int word_mutators_active(void) {
    return word_mutator_order != WORD_ORDER_NORMAL || word_mutator_case != LETTER_CASE_NORMAL;
}

static uint32_t text_seed(const wchar_t *text, int length) {
    uint32_t hash = 0x811c9dc5u;
    for (int i = 0; i < length; i++) hash = (hash ^ (uint32_t)text[i]) * 0x01000193u;
    return hash ? hash : 1;
}

static uint32_t next_random(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void reverse_text(wchar_t *text, int length) {
    for (int i = 0, j = length - 1; i < j; i++, j--) {
        wchar_t swap = text[i];
        text[i] = text[j];
        text[j] = swap;
    }
}

static int is_word_letter(wchar_t character) {
    return character != L' ' && character != L'\x3000' && !iswpunct(character);
}

static void scramble_inside_words(wchar_t *text, int length, uint32_t seed) {
    uint32_t state = seed;
    for (int start = 0; start < length;) {
        while (start < length && !is_word_letter(text[start])) start++;
        int end = start;
        while (end < length && is_word_letter(text[end])) end++;
        int inner_first = start + 1, inner_last = end - 2;
        for (int i = inner_last; i > inner_first; i--) {
            int j = inner_first + (int)(next_random(&state) % (uint32_t)(i - inner_first + 1));
            wchar_t swap = text[i];
            text[i] = text[j];
            text[j] = swap;
        }
        start = end;
    }
}

static void change_case(wchar_t *text, int length, uint32_t seed) {
    uint32_t state = seed;
    for (int i = 0; i < length; i++) {
        int upper = word_mutator_case == LETTER_CASE_UPPER   ? 1
                    : word_mutator_case == LETTER_CASE_LOWER ? 0
                                                             : (int)(next_random(&state) & 1);
        text[i] = upper ? towupper(text[i]) : towlower(text[i]);
    }
}

void mutate_word_text(wchar_t *display, int display_length, wchar_t *reading, int reading_length) {
    int same = display_length == reading_length && !memcmp(display, reading, display_length * sizeof(wchar_t));
    uint32_t seed = text_seed(display, display_length);
    if (word_mutator_order == WORD_ORDER_REVERSED) {
        reverse_text(display, display_length);
        if (!same) reverse_text(reading, reading_length);
    } else if (word_mutator_order == WORD_ORDER_SCRAMBLED && same) {
        scramble_inside_words(display, display_length, seed);
    }
    if (word_mutator_case != LETTER_CASE_NORMAL && same) change_case(display, display_length, seed);
    if (same) memcpy(reading, display, display_length * sizeof(wchar_t));
}
