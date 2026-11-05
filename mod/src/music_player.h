#pragma once
#include <stdint.h>

int music_player_play(const char *wave_path, int loop, uint64_t loop_start, uint64_t loop_end);
void music_player_stop(void);
void music_player_pause(int on);
int music_player_active(void);
int music_player_wave_rate(const char *wave_path, uint64_t *frames);
