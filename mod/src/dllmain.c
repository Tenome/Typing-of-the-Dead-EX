#include <windows.h>
#include <string.h>
#include "log.h"
#include "MinHook.h"
#include "dictionary_hooks.h"
#include "glyph_hooks.h"
#include "display_hooks.h"
#include "timing_hooks.h"
#include "overlay_hooks.h"
#include "import_profile.h"
#include "frame_interp.h"
#include "input_hooks.h"
#include "texture_hooks.h"
#include "music_hooks.h"
#include "language_hooks.h"
#include "model_hooks.h"
#include "cutscene_hooks.h"
#include "cheat_hooks.h"
#include "character_hooks.h"
#include "subtitle_hooks.h"
#include "randomizer.h"
#include "enemy_counts.h"
#include "online_hooks.h"
#include "frame_hooks.h"
#include "pause_menu.h"
#include "sound_loops.h"
#include "blood.h"
#include "volume.h"
#include "score_overlay.h"
#include "voice_packs.h"
#include "speech.h"
#include "background_mute.h"
#include "paintball.h"
#include "typewriter.h"
#include "achievements.h"
#include "skinned_models.h"
#include "memory_watch.h"
#include "mutators.h"
#include "typing_stats.h"
#include "replay.h"

#define TODMOD_VERSION "0.36.0"

static char game_folder[MAX_PATH];

static void resolve_game_folder(HINSTANCE self) {
    GetModuleFileNameA(self, game_folder, MAX_PATH);
    char *slash = strrchr(game_folder, '\\');
    if (slash) slash[1] = '\0';
}

static void build_path(char *out, const char *file_name) {
    strcpy(out, game_folder);
    strcat(out, file_name);
}

static void report_loaded_modules(void) {
    const char *names[] = {"Tod.exe", "HglCore.dll", "HglNa.dll", "HglHab.dll", "Table.dll", "Common_orig.dll"};
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        log_line("module %-16s base %p", names[i], (void *)GetModuleHandleA(names[i]));
    }
}

typedef struct {
    const char *module;
    DWORD timestamp;
    DWORD size_of_image;
} known_module;

static const known_module supported_build[] = {
    {"Tod.exe", 0x4DE72EF7, 0x643000},
    {"HglCore.dll", 0x42E6DBE3, 0x20000},
    {"HglNa.dll", 0x42E9E173, 0x121B000},
    {"Table.dll", 0x4DE72ED8, 0xC4000},
};

static int is_supported_game_build(void) {
    for (int i = 0; i < (int)(sizeof supported_build / sizeof supported_build[0]); i++) {
        const unsigned char *base = (const unsigned char *)GetModuleHandleA(supported_build[i].module);
        if (!base) {
            log_line("build check: %s is not loaded", supported_build[i].module);
            return 0;
        }
        const IMAGE_NT_HEADERS32 *headers = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
        if (headers->FileHeader.TimeDateStamp != supported_build[i].timestamp ||
            headers->OptionalHeader.SizeOfImage != supported_build[i].size_of_image) {
            log_line("build check: %s is a different build (timestamp %08lX, size %lX); todmod stays inactive",
                     supported_build[i].module, headers->FileHeader.TimeDateStamp, headers->OptionalHeader.SizeOfImage);
            return 0;
        }
    }
    return 1;
}

static void start_mod(HINSTANCE self) {
    char path[MAX_PATH];
    resolve_game_folder(self);
    build_path(path, "todmod.ini");
    int online_role = online_role_from_settings(path);
    build_path(path, online_log_name(online_role));
    log_open(path);
    log_line("todmod %s loaded from %s", TODMOD_VERSION, game_folder);
    report_loaded_modules();
    if (!is_supported_game_build()) return;
    MH_STATUS status = MH_Initialize();
    log_line("MinHook initialise: %s", MH_StatusToString(status));
    if (status != MH_OK) return;
    build_path(path, "todmod.ini");
    install_display_hooks(path);
    install_timing_hooks(path);
    install_overlay_hooks(path);
    install_import_profile(path);
    install_frame_interpolation(path);
    install_glyph_hooks(path);
    install_input_hooks(path);
    install_dictionary_hooks(game_folder, path);
    install_blood_colour(path);
    install_texture_hooks(game_folder, path);
    install_music_hooks(game_folder, path);
    install_language_hooks(path);
    install_model_hooks(game_folder, path);
    install_skinned_models();
    install_cutscene_hooks(path);
    install_character_hooks(path);
    replay_prepare(game_folder, path, TODMOD_VERSION);
    install_randomizer(path);
    install_cheat_hooks(path);
    install_online_hooks(path, online_role, TODMOD_VERSION);
    install_replay();
    install_mutators(path);
    install_typing_stats(game_folder, path);
    install_achievements(game_folder, path);
    install_memory_watch(game_folder, path);
    install_pause_menu(path);
    install_sound_loops(path);
    install_volume(path);
    install_score_overlay(path);
    install_speech(path);
    install_background_mute(path);
    install_paintball();
    install_typewriter(game_folder, path);
    install_voice_packs(game_folder, path);
    install_subtitle_hooks(game_folder, path);
    install_frame_hooks();
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        start_mod(self);
    } else if (reason == DLL_PROCESS_DETACH) {
        memory_watch_write();
        achievements_finish();
        typing_stats_finish();
        enemy_counts_finish();
        replay_finish();
        log_line("todmod: game closed normally%s", reserved ? "" : " (DLL unloaded)");
        MH_Uninitialize();
    }
    return TRUE;
}
