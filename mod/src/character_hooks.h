#pragma once
void install_character_hooks(const char *ini_path);
int character_override_active(void);
void character_load_extra_banks(int bank, void(__cdecl *request_bank_load)(int bank));
int character_keeps_bank(int bank);
