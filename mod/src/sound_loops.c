#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sound_loops.h"
#include "voice_packs.h"
#include "log.h"
#include "volume.h"
#include "frame_hooks.h"
#include "MinHook.h"

#define STOP_LOOPED_SOUNDS 0x300a0
#define STOP_LOOPED_SOUNDS_TOO 0x1100a0
#define ALL_SOUND_OFF 0x100a0
#define MAX_WAVE_BYTES (8 * 1024 * 1024)
#define GAME_MODE (*(volatile int *)0x00a3e308)
#define GAMEPLAY_MODE 6

typedef struct {
    const char *file;
    uint32_t loop_start, loop_end;
} sound_loop;

static const sound_loop loops[] = {
    {"CHAIN_SAW_22.WAV", 9542, 13162},
};

typedef BOOL(__cdecl *load_play_fn)(const char *path, int loop, unsigned channel, unsigned id);
typedef int(__cdecl *system_control_fn)(int unused, unsigned command);
static load_play_fn original_load_play;
static system_control_fn original_system_control;
static HWAVEOUT device;
static WAVEHDR intro, repeat;
static unsigned char *samples;
static CRITICAL_SECTION lock;
static int plays_logged;

static const sound_loop *loop_for(const char *path) {
    const char *name = path ? strrchr(path, '/') : NULL;
    const char *backslash = path ? strrchr(path, '\\') : NULL;
    if (backslash > name) name = backslash;
    name = name ? name + 1 : path;
    for (size_t i = 0; name && i < sizeof loops / sizeof *loops; i++) {
        if (_stricmp(name, loops[i].file) == 0) return &loops[i];
    }
    return NULL;
}

static void stop_loop_locked(void) {
    if (!device) return;
    waveOutReset(device);
    waveOutUnprepareHeader(device, &intro, sizeof intro);
    waveOutUnprepareHeader(device, &repeat, sizeof repeat);
    waveOutClose(device);
    device = NULL;
    free(samples);
    samples = NULL;
}

static int read_wave(const char *path, WAVEFORMATEX *format, unsigned char **data, uint32_t *bytes) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    unsigned char header[12];
    int ok = fread(header, 1, 12, file) == 12 && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVE", 4);
    int have_format = 0;
    *data = NULL;
    while (ok) {
        unsigned char chunk[8];
        if (fread(chunk, 1, 8, file) != 8) break;
        uint32_t size;
        memcpy(&size, chunk + 4, 4);
        if (!memcmp(chunk, "fmt ", 4) && size >= 16) {
            unsigned char fmt[16];
            ok = fread(fmt, 1, 16, file) == 16;
            memset(format, 0, sizeof *format);
            memcpy(format, fmt, 16);
            have_format = format->wFormatTag == WAVE_FORMAT_PCM;
            fseek(file, (long)(size - 16 + (size & 1)), SEEK_CUR);
        } else if (!memcmp(chunk, "data", 4) && size <= MAX_WAVE_BYTES) {
            *data = malloc(size);
            ok = *data && fread(*data, 1, size, file) == size;
            *bytes = size;
            break;
        } else {
            fseek(file, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    fclose(file);
    if (ok && have_format && *data) return 1;
    free(*data);
    *data = NULL;
    return 0;
}

static int play_loop(const char *path, const sound_loop *loop) {
    WAVEFORMATEX format;
    unsigned char *data;
    uint32_t bytes;
    if (!read_wave(path, &format, &data, &bytes)) return 0;
    if (format.wBitsPerSample == 16) volume_scale_samples((int16_t *)data, bytes / 2, volume_effects_factor());
    uint32_t frame = format.nBlockAlign, frames = bytes / frame;
    if (!frame || loop->loop_end > frames || loop->loop_start >= loop->loop_end) {
        free(data);
        return 0;
    }
    EnterCriticalSection(&lock);
    stop_loop_locked();
    if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        device = NULL;
        LeaveCriticalSection(&lock);
        free(data);
        return 0;
    }
    samples = data;
    memset(&intro, 0, sizeof intro);
    memset(&repeat, 0, sizeof repeat);
    intro.lpData = (LPSTR)samples;
    intro.dwBufferLength = loop->loop_start * frame;
    repeat.lpData = (LPSTR)samples + loop->loop_start * frame;
    repeat.dwBufferLength = (loop->loop_end - loop->loop_start) * frame;
    repeat.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    repeat.dwLoops = 0xFFFFFFFF;
    waveOutPrepareHeader(device, &intro, sizeof intro);
    waveOutPrepareHeader(device, &repeat, sizeof repeat);
    repeat.dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
    if (intro.dwBufferLength) waveOutWrite(device, &intro, sizeof intro);
    waveOutWrite(device, &repeat, sizeof repeat);
    LeaveCriticalSection(&lock);
    return 1;
}

static BOOL __cdecl load_play_hook(const char *path, int loop, unsigned channel, unsigned id) {
    path = voice_packs_redirect(path);
    const sound_loop *region = loop ? loop_for(path) : NULL;
    if (region && play_loop(path, region)) {
        if (plays_logged++ < 8) log_line("sound: %s with its Dreamcast loop (intro once, frames %u-%u repeat)", path,
                                         region->loop_start, region->loop_end);
        return TRUE;
    }
    return original_load_play(path, loop, channel, id);
}

static int __cdecl system_control_hook(int unused, unsigned command) {
    if (command == STOP_LOOPED_SOUNDS || command == STOP_LOOPED_SOUNDS_TOO || command == ALL_SOUND_OFF) {
        EnterCriticalSection(&lock);
        stop_loop_locked();
        LeaveCriticalSection(&lock);
    }
    return original_system_control(unused, command);
}

typedef void(__cdecl *play_game_sound_fn)(unsigned id);
#define PLAY_GAME_SOUND ((play_game_sound_fn)0x0049c380)
#define CHAINSAW_ON 0x4d17a9
#define CHAINSAW_OFF 0x4e17a9
static int test_frames = -1;

static void run_sound_test(void) {
    if (test_frames < 0 || GAME_MODE != GAMEPLAY_MODE) return;
    test_frames++;
    if (test_frames == 600) {
        log_line("sound test: chainsaw on");
        PLAY_GAME_SOUND(CHAINSAW_ON);
        log_line("sound test: own loop %s", device ? "playing" : "not playing");
    }
    if (test_frames == 1200) {
        PLAY_GAME_SOUND(CHAINSAW_OFF);
        log_line("sound test: chainsaw off, own loop %s", device ? "still playing" : "stopped");
        test_frames = -1;
    }
}

static void stop_outside_gameplay(void) {
    if (device && GAME_MODE != GAMEPLAY_MODE) sound_loops_stop();
}

int sound_loops_hook_load_play(void) {
    return original_load_play != NULL;
}

void sound_loops_pause(int on) {
    if (!original_load_play) return;
    EnterCriticalSection(&lock);
    if (device) {
        if (on) waveOutPause(device);
        else waveOutRestart(device);
    }
    LeaveCriticalSection(&lock);
}

void sound_loops_stop(void) {
    if (!original_load_play) return;
    EnterCriticalSection(&lock);
    stop_loop_locked();
    LeaveCriticalSection(&lock);
}

void install_sound_loops(const char *ini_path) {
    if (!GetPrivateProfileIntA("sound", "dreamcast_loops", 1, ini_path)) return;
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    void *load_play = hglna ? (void *)GetProcAddress(hglna, "nlsndLoadPlay") : NULL;
    void *system_control = hglna ? (void *)GetProcAddress(hglna, "nlsndSystemControl") : NULL;
    if (!load_play || !system_control) return;
    InitializeCriticalSection(&lock);
    MH_STATUS status = MH_CreateHook(load_play, load_play_hook, (void **)&original_load_play);
    if (status == MH_OK) status = MH_EnableHook(load_play);
    MH_STATUS control = MH_CreateHook(system_control, system_control_hook, (void **)&original_system_control);
    if (control == MH_OK) control = MH_EnableHook(system_control);
    frame_hooks_on_frame_start(stop_outside_gameplay);
    if (GetPrivateProfileIntA("debug", "sound_loop_test", 0, ini_path)) {
        test_frames = 0;
        frame_hooks_on_frame_end(run_sound_test);
    }
    log_line("sound: Dreamcast loop points for the chainsaw (hooks nlsndLoadPlay %s, nlsndSystemControl %s)",
             MH_StatusToString(status), MH_StatusToString(control));
}
