#pragma once
#include <stdint.h>

int game_models_open(const char *game_folder);
void game_models_close(void);
int game_models_kind_count(void);
int game_models_render_character(int kind, uint32_t *pixels, int width, int height, float yaw);
void game_models_set_packs(const char *pack_list);
const char *game_models_pack_name(int kind);
