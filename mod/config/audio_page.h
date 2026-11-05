#pragma once
#include <windows.h>

void audio_page_create(HWND dialog, RECT area, const char *game_folder);
void audio_page_show(int visible);
int audio_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
void audio_page_close(void);
