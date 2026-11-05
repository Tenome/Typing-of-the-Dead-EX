#pragma once
void install_frame_interpolation(const char *ini_path);
void frame_interp_capture_next_frame(void);
int frame_interp_frame_ready(void);
void frame_interp_set_frozen(int on);
void frame_interp_draw_frozen(void);
void frame_interp_draw_frozen_scene(void);
void frame_interp_log_frozen(void);
void frame_interp_log_frozen_matrices(void);
