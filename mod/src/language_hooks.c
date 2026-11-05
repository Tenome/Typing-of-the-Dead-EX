#include <windows.h>
#include "language_hooks.h"
#include "log.h"
#include "MinHook.h"

#define APPLY_SETTINGS_VA 0x0044fdc0
#define GAME_LANGUAGE (*(volatile short *)0x00a1b6b2)
#define LANGUAGE_ENGLISH 0

typedef void(__cdecl *apply_settings_fn)(void);

static apply_settings_fn original_apply_settings;

static void __cdecl apply_settings_hook(void) {
    original_apply_settings();
    GAME_LANGUAGE = LANGUAGE_ENGLISH;
}

void install_language_hooks(const char *ini_path) {
    char language[16];
    GetPrivateProfileStringA("text", "language", "japanese", language, sizeof language, ini_path);
    if (lstrcmpiA(language, "english") != 0) return;
    MH_STATUS status = MH_CreateHook((void *)APPLY_SETTINGS_VA, apply_settings_hook, (void **)&original_apply_settings);
    if (status == MH_OK) status = MH_EnableHook((void *)APPLY_SETTINGS_VA);
    log_line("language: English mode (subtitles from the game's English text; hook apply_settings: %s)",
             MH_StatusToString(status));
}
