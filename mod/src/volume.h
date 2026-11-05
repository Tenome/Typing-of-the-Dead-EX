#pragma once
#include <stddef.h>
#include <stdint.h>

void install_volume(const char *ini_path);
float volume_music_factor(void);
float volume_effects_factor(void);
void volume_scale_samples(int16_t *samples, size_t count, float factor);
void volume_apply_music(void);
