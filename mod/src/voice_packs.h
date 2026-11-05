#pragma once
void install_voice_packs(const char *game_folder, const char *ini_path);
const char *voice_packs_redirect(const char *path);
typedef void (*sound_observer)(const char *game_path);
typedef const char *(*sound_redirect)(const char *game_path);
void voice_packs_observe(sound_observer observer);
void voice_packs_add_redirect(sound_redirect redirect);
