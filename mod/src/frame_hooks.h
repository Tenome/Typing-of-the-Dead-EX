#pragma once

typedef void (*frame_callback)(void);
typedef int (*frame_pause_fn)(void);

void frame_hooks_on_frame_start(frame_callback callback);
void frame_hooks_on_frame_end(frame_callback callback);
void frame_hooks_set_pause_handler(frame_pause_fn handler);
void install_frame_hooks(void);
