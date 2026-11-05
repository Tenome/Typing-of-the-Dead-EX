#pragma once
#include <stdint.h>

void install_randomizer(const char *ini_path);
int randomizer_active(void);
uint32_t randomizer_next(void);
void randomizer_load_extra_banks(int bank, void(__cdecl *request_bank_load)(int bank));
int randomizer_keeps_bank(int bank);
