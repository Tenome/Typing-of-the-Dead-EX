#include <windows.h>
#include "cutscene_hooks.h"
#include "log.h"
#include "MinHook.h"

#define SKIP_CAPTION_TASK_VA 0x004261a0
#define CUTSCENE_SKIPPABLE (*(volatile int *)0x00a18fa8)
#define SKIP_REQUESTED (*(volatile unsigned char *)0x00a3e808)
#define GAME_MODE (*(volatile int *)0x00a3e308)
#define FRAMES_BEFORE_SKIP 30

typedef void(__cdecl *skip_caption_task_fn)(void *task);

static skip_caption_task_fn original_skip_caption_task;
static unsigned skipped;
static void *current_task;
static unsigned task_frames;

static int is_title_or_demo(int mode) {
    return mode == 3 || mode == 5 || mode == 9 || mode == 10 || mode == 11;
}

static void __cdecl skip_caption_task_hook(void *task) {
    if (task != current_task) {
        current_task = task;
        task_frames = 0;
    }
    task_frames++;
    if (task_frames >= FRAMES_BEFORE_SKIP && CUTSCENE_SKIPPABLE && !SKIP_REQUESTED && !is_title_or_demo(GAME_MODE)) {
        SKIP_REQUESTED = 1;
        log_line("cutscene: skipped (%u)", ++skipped);
    }
    original_skip_caption_task(task);
}

void install_cutscene_hooks(const char *ini_path) {
    if (!GetPrivateProfileIntA("gameplay", "skip_cutscenes", 0, ini_path)) return;
    MH_STATUS status = MH_CreateHook((void *)SKIP_CAPTION_TASK_VA, skip_caption_task_hook,
                                     (void **)&original_skip_caption_task);
    if (status == MH_OK) status = MH_EnableHook((void *)SKIP_CAPTION_TASK_VA);
    log_line("cutscene: skipping cutscenes automatically (hook skip_caption_task: %s)", MH_StatusToString(status));
}
