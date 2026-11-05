#pragma once
#include <stdint.h>

void replay_prepare(const char *game_folder, const char *ini_path, const char *version);
int replay_forced_randomizer_seed(uint32_t *seed);
void replay_note_randomizer_seed(uint32_t seed);
void install_replay(void);
int replay_active(void);
int replay_playing(void);
void replay_finish(void);
