#pragma once
#include <stdint.h>

void install_blood_colour(const char *ini_path);
int blood_needs_recolouring(void);
uint16_t *blood_recolour(const char *bank_name, int index, int width, int height, int pixel_format, const void *pixels);
int blood_is_paintball(void);
