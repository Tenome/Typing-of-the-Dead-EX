#pragma once
#include <stddef.h>
#include <stdint.h>

void install_model_hooks(const char *game_folder, const char *ini_path);
int model_hooks_pack_folder(int index, char *out, size_t size);
size_t model_hooks_object_size(const uint32_t *object);
uint32_t *model_hooks_replacement(int model_id);
