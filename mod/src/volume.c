#include <windows.h>
#include <math.h>
#include <stdint.h>
#include "volume.h"
#include "log.h"
#include "MinHook.h"

#define VOICE_CHANNEL 0x10
#define HGLCORE_GHIDRA_BASE 0x10000000
#define MUSIC_GRAPH_VA 0x1001d8e0
#define BASIC_AUDIO_PUT_VOLUME 7
#define COM_RELEASE 2
#define SILENT_DB (-10000)
#define FULL 100

typedef int(__cdecl *sound_play_fn)(int channel);
typedef int(__cdecl *set_sound_volume_fn)(int channel, int hundredths_db);
typedef HRESULT(__stdcall *query_interface_fn)(void *self, const GUID *iid, void **out);
typedef HRESULT(__stdcall *put_volume_fn)(void *self, long hundredths_db);
typedef ULONG(__stdcall *release_fn)(void *self);

static const GUID IID_BASIC_AUDIO = {0x56a868b3, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
static int music_percent = FULL, effects_percent = FULL, voice_percent = FULL;
static sound_play_fn original_sound_play;
static set_sound_volume_fn set_sound_volume;
static void *const *music_graph;

static int clamp_percent(int percent) {
    return percent < 0 ? 0 : percent > FULL ? FULL : percent;
}

static long hundredths_db(int percent) {
    if (percent <= 0) return SILENT_DB;
    long db = (long)lround(2000.0 * log10(percent / 100.0));
    return db < SILENT_DB ? SILENT_DB : db;
}

float volume_music_factor(void) {
    return music_percent / 100.0f;
}

float volume_effects_factor(void) {
    return effects_percent / 100.0f;
}

void volume_scale_samples(int16_t *samples, size_t count, float factor) {
    if (factor >= 0.999f) return;
    for (size_t i = 0; i < count; i++) samples[i] = (int16_t)lroundf(samples[i] * factor);
}

void volume_apply_music(void) {
    if (music_percent == FULL || !music_graph || !*music_graph) return;
    void *graph = *music_graph, *audio = NULL;
    query_interface_fn query = (query_interface_fn)(*(void ***)graph)[0];
    if (FAILED(query(graph, &IID_BASIC_AUDIO, &audio)) || !audio) return;
    HRESULT result = ((put_volume_fn)(*(void ***)audio)[BASIC_AUDIO_PUT_VOLUME])(audio, hundredths_db(music_percent));
    static int logged;
    if (!logged++) log_line("sound: music volume set on the DirectShow graph (%ld, result %08lX)", hundredths_db(music_percent), (unsigned long)result);
    ((release_fn)(*(void ***)audio)[COM_RELEASE])(audio);
}

static int __cdecl sound_play_hook(int channel) {
    int percent = channel == VOICE_CHANNEL ? voice_percent : effects_percent;
    if (set_sound_volume) set_sound_volume(channel, (int)hundredths_db(percent));
    static int logged;
    if (logged < 2 && (channel == VOICE_CHANNEL) == logged) {
        log_line("sound: channel %d volume %ld", channel, hundredths_db(percent));
        logged++;
    }
    return original_sound_play(channel);
}

void install_volume(const char *ini_path) {
    music_percent = clamp_percent(GetPrivateProfileIntA("sound", "music_volume", FULL, ini_path));
    effects_percent = clamp_percent(GetPrivateProfileIntA("sound", "effects_volume", FULL, ini_path));
    voice_percent = clamp_percent(GetPrivateProfileIntA("sound", "voice_volume", FULL, ini_path));
    if (music_percent == FULL && effects_percent == FULL && voice_percent == FULL) return;
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    if (!hglcore) return;
    music_graph = (void *const *)((char *)hglcore + (MUSIC_GRAPH_VA - HGLCORE_GHIDRA_BASE));
    set_sound_volume = (set_sound_volume_fn)GetProcAddress(hglcore, "hlSetSoundVolume");
    void *play = (void *)GetProcAddress(hglcore, "hlSoundPlay");
    MH_STATUS status = play && set_sound_volume ? MH_CreateHook(play, sound_play_hook, (void **)&original_sound_play)
                                                : MH_ERROR_FUNCTION_NOT_FOUND;
    if (status == MH_OK) status = MH_EnableHook(play);
    log_line("sound: volume music %d%%, effects %d%%, voices %d%% (hook hlSoundPlay: %s)", music_percent,
             effects_percent, voice_percent, MH_StatusToString(status));
}
