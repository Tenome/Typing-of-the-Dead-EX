#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "voice_packs.h"
#include "music_hooks.h"
#include "sound_loops.h"
#include "log.h"
#include "MinHook.h"

#define MAX_PACKS 8
#define PACK_NAME_LENGTH 64
#define SOUND_PREFIX "sound/"
#define VOICE_PREFIX "voice/"
#define REPLACEMENTS_LOGGED 32

typedef BOOL(__cdecl *load_play_fn)(const char *path, int loop, unsigned channel, unsigned id);

static const char *const extensions[] = {"wav", "ogg", "flac"};
#define EXTENSION_COUNT ((int)(sizeof extensions / sizeof extensions[0]))

static char voices_folder[MAX_PATH], cache_folder[MAX_PATH];
static char pack_names[MAX_PACKS][PACK_NAME_LENGTH];
static int pack_count, replacements_logged;
static char redirected[MAX_PATH];
static load_play_fn original_load_play;
static CRITICAL_SECTION decode_lock;
static sound_observer observers[4];
static sound_redirect redirects[4];
static int observer_count, redirect_count;

void voice_packs_observe(sound_observer observer) {
    if (observer_count < 4) observers[observer_count++] = observer;
}

void voice_packs_add_redirect(sound_redirect redirect) {
    if (redirect_count < 4) redirects[redirect_count++] = redirect;
}

static int file_exists(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void read_pack_list(const char *ini_path) {
    char list[MAX_PACKS * PACK_NAME_LENGTH];
    GetPrivateProfileStringA("sound", "voice_packs", "", list, sizeof list, ini_path);
    for (char *name = strtok(list, ","); name && pack_count < MAX_PACKS; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (*name) lstrcpynA(pack_names[pack_count++], name, PACK_NAME_LENGTH);
    }
}

static int is_decoded_format(const char *extension) {
    return _stricmp(extension, "wav") != 0;
}

static int playable_copy(const char *source, const char *pack, const char *relative, char *out, size_t size) {
    char flat[MAX_PATH];
    lstrcpynA(flat, relative, sizeof flat);
    for (char *c = flat; *c; c++) {
        if (*c == '/' || *c == '\\' || *c == '.') *c = '_';
    }
    snprintf(out, size, "%s%s_%s.wav", cache_folder, pack, flat);
    EnterCriticalSection(&decode_lock);
    WIN32_FILE_ATTRIBUTE_DATA source_data, cache_data;
    int ready = GetFileAttributesExA(out, GetFileExInfoStandard, &cache_data) &&
                GetFileAttributesExA(source, GetFileExInfoStandard, &source_data) &&
                CompareFileTime(&cache_data.ftLastWriteTime, &source_data.ftLastWriteTime) >= 0;
    if (!ready) {
        CreateDirectoryA(cache_folder, NULL);
        ready = music_decode_file(source, out);
    }
    LeaveCriticalSection(&decode_lock);
    return ready;
}

static int find_in_pack(const char *pack, const char *relative, char *out, size_t size) {
    char stem[MAX_PATH];
    lstrcpynA(stem, relative, sizeof stem);
    char *dot = strrchr(stem, '.'), *slash = strrchr(stem, '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    for (int i = 0; i < EXTENSION_COUNT; i++) {
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof candidate, "%s%s\\%s.%s", voices_folder, pack, stem, extensions[i]);
        for (char *c = candidate; *c; c++) {
            if (*c == '/') *c = '\\';
        }
        if (!file_exists(candidate)) continue;
        if (!is_decoded_format(extensions[i])) {
            lstrcpynA(out, candidate, (int)size);
            return 1;
        }
        if (playable_copy(candidate, pack, stem, out, size)) return 1;
    }
    return 0;
}

const char *voice_packs_redirect(const char *path) {
    for (int i = 0; path && i < observer_count; i++) observers[i](path);
    for (int i = 0; path && i < redirect_count; i++) {
        const char *replacement = redirects[i](path);
        if (replacement) return replacement;
    }
    if (!pack_count || !path || _strnicmp(path, SOUND_PREFIX, strlen(SOUND_PREFIX)) != 0) return path;
    const char *relative = path + strlen(SOUND_PREFIX);
    const char *below_voice = _strnicmp(relative, VOICE_PREFIX, strlen(VOICE_PREFIX)) == 0 ? relative + strlen(VOICE_PREFIX) : NULL;
    for (int pack = 0; pack < pack_count; pack++) {
        if (find_in_pack(pack_names[pack], relative, redirected, sizeof redirected) ||
            (below_voice && find_in_pack(pack_names[pack], below_voice, redirected, sizeof redirected))) {
            if (replacements_logged++ < REPLACEMENTS_LOGGED) log_line("voice packs: %s -> %s", path, redirected);
            return redirected;
        }
    }
    return path;
}

static BOOL __cdecl load_play_hook(const char *path, int loop, unsigned channel, unsigned id) {
    return original_load_play(voice_packs_redirect(path), loop, channel, id);
}

static void decode_folder(const char *folder, const char *pack, const char *relative) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s\\*", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (found.cFileName[0] == '.') continue;
        char path[MAX_PATH], child[MAX_PATH];
        snprintf(path, sizeof path, "%s\\%s", folder, found.cFileName);
        snprintf(child, sizeof child, "%s%s%s", relative, *relative ? "/" : "", found.cFileName);
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            decode_folder(path, pack, child);
            continue;
        }
        char *dot = strrchr(child, '.');
        if (!dot || !is_decoded_format(dot + 1) || (_stricmp(dot + 1, "ogg") && _stricmp(dot + 1, "flac"))) continue;
        *dot = '\0';
        char cache[MAX_PATH];
        playable_copy(path, pack, child, cache, sizeof cache);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static DWORD WINAPI decode_packs_thread(LPVOID unused) {
    (void)unused;
    for (int pack = 0; pack < pack_count; pack++) {
        char folder[MAX_PATH];
        snprintf(folder, sizeof folder, "%s%s", voices_folder, pack_names[pack]);
        decode_folder(folder, pack_names[pack], "");
    }
    return 0;
}

void install_voice_packs(const char *game_folder, const char *ini_path) {
    read_pack_list(ini_path);
    if (!pack_count && !observer_count && !redirect_count) return;
    snprintf(voices_folder, sizeof voices_folder, "%svoices\\", game_folder);
    snprintf(cache_folder, sizeof cache_folder, "%s.cache\\", voices_folder);
    InitializeCriticalSection(&decode_lock);
    const char *hook = "shared with sound loops";
    if (!sound_loops_hook_load_play()) {
        HMODULE hglna = GetModuleHandleA("HglNa.dll");
        void *load_play = hglna ? (void *)GetProcAddress(hglna, "nlsndLoadPlay") : NULL;
        MH_STATUS status = load_play ? MH_CreateHook(load_play, load_play_hook, (void **)&original_load_play)
                                     : MH_ERROR_FUNCTION_NOT_FOUND;
        if (status == MH_OK) status = MH_EnableHook(load_play);
        hook = MH_StatusToString(status);
    }
    HANDLE thread = CreateThread(NULL, 0, decode_packs_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    log_line("voice packs: %d pack(s)%s%s (hook nlsndLoadPlay: %s)", pack_count, pack_count ? ", first " : "",
             pack_count ? pack_names[0] : "", hook);
}
