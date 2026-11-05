#include <windows.h>
#include <string.h>
#include "subtitle_text.h"

typedef struct {
    wchar_t character;
    const char *replacement;
} substitution;

static const substitution substitutions[] = {
    {L'ß', "ss"}, {L'Æ', "AE"}, {L'æ', "ae"}, {L'Œ', "OE"}, {L'œ', "oe"}, {L'Ø', "O"},
    {L'ø', "o"}, {L'Ł', "L"}, {L'ł', "l"}, {L'Đ', "D"}, {L'đ', "d"}, {L'Ð', "D"},
    {L'ð', "d"}, {L'Þ', "Th"}, {L'þ', "th"}, {L'"', "'"}, {L'“', "'"}, {L'”', "'"},
    {L'„', "'"}, {L'‘', "'"}, {L'’', "'"}, {L'«', "'"}, {L'»', "'"}, {L'`', "'"},
    {L'…', "..."}, {L'–', "-"}, {L'—', "-"}, {L':', " -"}, {L';', ","}, {L'(', "-"}, {L')', "-"},
    {L'/', "-"}, {L'¡', "!"}, {L'¿', "?"}, {L'_', "-"}, {L' ', " "},
};

int subtitle_character_supported(int character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || (character && strchr(" !',-.?~", character) != NULL);
}

static int append(char *out, size_t size, size_t *used, const char *text) {
    size_t length = strlen(text);
    if (*used + length + 1 > size) return 0;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return 1;
}

static const char *game_text_for_character(wchar_t character, char single[2]) {
    if (character < 0x80 && subtitle_character_supported(character)) {
        single[0] = (char)character;
        return single;
    }
    for (size_t s = 0; s < sizeof substitutions / sizeof substitutions[0]; s++) {
        if (substitutions[s].character == character) return substitutions[s].replacement;
    }
    wchar_t folded[4] = {0};
    if (FoldStringW(MAP_COMPOSITE, &character, 1, folded, 4) > 0 && folded[0] < 0x80 &&
        subtitle_character_supported(folded[0])) {
        single[0] = (char)folded[0];
        return single;
    }
    return NULL;
}

int subtitle_needs_windows_font(const char *utf8) {
    wchar_t wide[SUBTITLE_MAX_TEXT];
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, SUBTITLE_MAX_TEXT);
    for (int i = 0; i < length && wide[i]; i++) {
        char single[2] = {0, 0};
        if (wide[i] != L'|' && !game_text_for_character(wide[i], single)) return 1;
    }
    return 0;
}

int subtitle_to_game_text(const char *utf8, char *out, size_t size) {
    wchar_t wide[SUBTITLE_MAX_TEXT];
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, SUBTITLE_MAX_TEXT);
    size_t used = 0;
    int substituted = 0;
    out[0] = '\0';
    for (int i = 0; i < length && wide[i]; i++) {
        char single[2] = {0, 0};
        if (wide[i] == L'|') {
            append(out, size, &used, "\n");
            continue;
        }
        const char *replacement = game_text_for_character(wide[i], single);
        if (replacement != single || wide[i] >= 0x80) substituted++;
        append(out, size, &used, replacement ? replacement : "?");
    }
    return substituted;
}
