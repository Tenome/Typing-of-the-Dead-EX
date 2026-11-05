#pragma once
void install_overlay_hooks(const char *ini_path);
void overlay_set_object_drawn_callback(void *(*callback)(void *object));
