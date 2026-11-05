#pragma once
void install_subtitle_hooks(const char *game_folder, const char *ini_path);
void subtitle_draw_overlay(void);
#include <windows.h>
typedef int (*overlay_draw_fn)(HDC dc, int width, int height);
void overlay_add(overlay_draw_fn draw);
