#pragma once
#include <windows.h>

#include "randomizer_page.h"
#include "ini_file.h"
void character_page_create(HWND dialog, const char *game_folder, const char *subtitle_file,
                           const randomizer_settings *random_settings, const ini_file *todmod_ini);
void character_page_open_tab(HWND dialog, int page);
void character_page_destroy(void);
void character_page_open(HWND dialog, int kind);
int character_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
