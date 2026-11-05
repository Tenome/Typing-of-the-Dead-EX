#include <windows.h>
#include "frame_hooks.h"
#include "log.h"
#include "MinHook.h"

#define GAME_FRAME_VA 0x004755b0
#define MAX_CALLBACKS 16

typedef void(__cdecl *game_frame_fn)(void);

static game_frame_fn original_game_frame;
static frame_callback start_callbacks[MAX_CALLBACKS], end_callbacks[MAX_CALLBACKS];
static int start_count, end_count;
static frame_pause_fn pause_handler;

void frame_hooks_on_frame_start(frame_callback callback) {
    if (start_count < MAX_CALLBACKS) start_callbacks[start_count++] = callback;
}

void frame_hooks_on_frame_end(frame_callback callback) {
    if (end_count < MAX_CALLBACKS) end_callbacks[end_count++] = callback;
}

void frame_hooks_set_pause_handler(frame_pause_fn handler) {
    pause_handler = handler;
}

static void __cdecl game_frame_hook(void) {
    if (pause_handler && pause_handler()) return;
    for (int i = 0; i < start_count; i++) start_callbacks[i]();
    original_game_frame();
    for (int i = 0; i < end_count; i++) end_callbacks[i]();
}

void install_frame_hooks(void) {
    if (!start_count && !end_count && !pause_handler) return;
    MH_STATUS status = MH_CreateHook((void *)GAME_FRAME_VA, game_frame_hook, (void **)&original_game_frame);
    if (status == MH_OK) status = MH_EnableHook((void *)GAME_FRAME_VA);
    log_line("frame: hook game_frame %s (%d start, %d end callbacks)", MH_StatusToString(status), start_count, end_count);
}
