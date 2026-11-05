#pragma once

void install_sound_loops(const char *ini_path);
void sound_loops_stop(void);
void sound_loops_pause(int on);
int sound_loops_hook_load_play(void);
