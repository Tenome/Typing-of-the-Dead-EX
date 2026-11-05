#pragma once
#include <windows.h>

void subtitle_page_create(HWND dialog, const char *game_folder, RECT area, const char *current_file);
void subtitle_page_show(int visible);
void subtitle_page_chosen_file(char *out, size_t size);
void subtitle_page_set_font(const char *face, int all_lines);
void subtitle_page_font(char *face, size_t size, int *all_lines);
int subtitle_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
void subtitle_page_set_cover_voice(int on);
int subtitle_page_cover_voice(void);
void subtitle_page_set_layout(int size_percent, int position_percent);
void subtitle_page_layout(int *size_percent, int *position_percent);
