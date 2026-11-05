#pragma once
void install_dictionary_hooks(const char *game_folder, const char *ini_path);
#include <wchar.h>
const wchar_t *dictionary_half_width_cell(const void *display_cell, int *cell_index);
#include <stdint.h>
const uint8_t *dictionary_letter_cases(const uint16_t *reading);
int dictionary_quiz_answer(const uint16_t *reading);
