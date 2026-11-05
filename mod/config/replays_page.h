#pragma once
#include <windows.h>
#include "ini_file.h"

void replays_page_create(HWND dialog, RECT area, const char *game_folder, const ini_file *ini);
void replays_page_show(int visible);
void replays_page_save(ini_file *ini);
int replays_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
