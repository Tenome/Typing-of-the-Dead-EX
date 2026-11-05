#pragma once
#include <windows.h>

void stats_page_create(HWND dialog, RECT area, const char *game_folder);
void stats_page_show(int visible);
int stats_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
void stats_page_set_practice_callback(void (*callback)(const char *pack_name));
