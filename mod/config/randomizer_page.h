#pragma once
#include <windows.h>

typedef struct {
    int enabled, enemies, people, mix, voices, items, weapons;
    unsigned seed;
    int human_zombies, enemy_min, enemy_max;
} randomizer_settings;

void randomizer_page_create(HWND dialog, RECT area, const randomizer_settings *settings);
void randomizer_page_show(int visible);
void randomizer_page_settings(randomizer_settings *settings);
int randomizer_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
