#pragma once
#include <stddef.h>

#define SUBTITLE_GAME_COUNT 366
#define SUBTITLE_INTRO_FIRST 366
#define SUBTITLE_INTRO_COUNT 9
#define SUBTITLE_INTRO_FIRST_ID 392
#define SUBTITLE_INTRO_EMPTY_RECORD 364
#define SUBTITLE_COUNT (SUBTITLE_GAME_COUNT + SUBTITLE_INTRO_COUNT)
#define SUBTITLE_MAX_LINE 57
#define SUBTITLE_MAX_TEXT 512
#define SUBTITLE_TOD_VA 0x0051eeac
#define SUBTITLE_TOD_STRIDE 0x40
#define SUBTITLE_TOD_TEXT 0x38

int subtitle_character_supported(int character);
int subtitle_needs_windows_font(const char *utf8);
int subtitle_to_game_text(const char *utf8, char *out, size_t size);
