#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "music_hooks.h"
#include "log.h"
#include "volume.h"
#include "MinHook.h"
#include "music_player.h"

#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb/stb_vorbis.c"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_WCHAR
#include "../third_party/dr_libs/dr_flac.h"

#define MAX_PACKS 16
#define PACK_NAME_LENGTH 64
#define TRACK_NAME_LENGTH 64
#define BITS_PER_SAMPLE 16
#define WAVE_FORMAT_PCM_TAG 1

typedef char(__cdecl *wave_play_fn)(const wchar_t *path, int loop);
typedef char(__cdecl *wave_stop_fn)(void);

typedef enum { MUSIC_PLAYS_DIRECTLY, MUSIC_NEEDS_DECODING } music_kind;

typedef struct {
    const char *extension;
    music_kind kind;
} music_format;

static const music_format music_formats[] = {
    {"wav", MUSIC_PLAYS_DIRECTLY}, {"mp3", MUSIC_PLAYS_DIRECTLY}, {"ogg", MUSIC_NEEDS_DECODING}, {"flac", MUSIC_NEEDS_DECODING}};
#define MUSIC_FORMAT_COUNT ((int)(sizeof music_formats / sizeof music_formats[0]))

static wave_play_fn original_wave_play;
static wave_stop_fn original_wave_stop;
static char game_folder_path[MAX_PATH];
static char music_folder[MAX_PATH];
static char cache_folder[MAX_PATH];
static char pack_names[MAX_PACKS][PACK_NAME_LENGTH];
static int pack_count;
static CRITICAL_SECTION decode_lock;

static void read_pack_list(const char *ini_path) {
    char list[MAX_PACKS * PACK_NAME_LENGTH];
    GetPrivateProfileStringA("music", "packs", "", list, sizeof list, ini_path);
    for (char *name = strtok(list, ","); name && pack_count < MAX_PACKS; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (*name) lstrcpynA(pack_names[pack_count++], name, PACK_NAME_LENGTH);
    }
}

static void track_name_from_path(const wchar_t *path, char *name, size_t size) {
    const wchar_t *file = path;
    for (const wchar_t *c = path; *c; c++) {
        if (*c == L'/' || *c == L'\\') file = c + 1;
    }
    WideCharToMultiByte(CP_ACP, 0, file, -1, name, (int)size, NULL, NULL);
    char *extension = strrchr(name, '.');
    if (extension) *extension = '\0';
}

static int file_exists(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int find_in_folder(const char *folder, const char *track, char *path, size_t size, int *format) {
    for (int i = 0; i < MUSIC_FORMAT_COUNT; i++) {
        snprintf(path, size, "%s%s.%s", folder, track, music_formats[i].extension);
        if (file_exists(path)) {
            *format = i;
            return 1;
        }
    }
    return 0;
}

static int find_replacement(const char *track, char *path, size_t size, int *format, char *source, size_t source_size) {
    char folder[MAX_PATH];
    for (int pack = 0; pack < pack_count; pack++) {
        snprintf(folder, sizeof folder, "%s%s\\", music_folder, pack_names[pack]);
        if (find_in_folder(folder, track, path, size, format)) {
            lstrcpynA(source, pack_names[pack], (int)source_size);
            return 1;
        }
    }
    snprintf(folder, sizeof folder, "%sSOUND\\BGM\\", game_folder_path);
    if (find_in_folder(folder, track, path, size, format)) {
        lstrcpynA(source, "BGM", (int)source_size);
        return 1;
    }
    return 0;
}

static void write_u32(FILE *file, uint32_t value) { fwrite(&value, 4, 1, file); }
static void write_u16(FILE *file, uint16_t value) { fwrite(&value, 2, 1, file); }

static int write_wave_file(const char *path, const int16_t *samples, uint64_t frames, int channels, int sample_rate) {
    uint64_t data_bytes = frames * (uint64_t)channels * sizeof(int16_t);
    if (data_bytes > 0xFFFFFFF0u - 36) return 0;
    char temporary[MAX_PATH];
    snprintf(temporary, sizeof temporary, "%s.part", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) return 0;
    fwrite("RIFF", 1, 4, file);
    write_u32(file, (uint32_t)(36 + data_bytes));
    fwrite("WAVEfmt ", 1, 8, file);
    write_u32(file, 16);
    write_u16(file, WAVE_FORMAT_PCM_TAG);
    write_u16(file, (uint16_t)channels);
    write_u32(file, (uint32_t)sample_rate);
    write_u32(file, (uint32_t)(sample_rate * channels * sizeof(int16_t)));
    write_u16(file, (uint16_t)(channels * sizeof(int16_t)));
    write_u16(file, BITS_PER_SAMPLE);
    fwrite("data", 1, 4, file);
    write_u32(file, (uint32_t)data_bytes);
    int written = fwrite(samples, 1, (size_t)data_bytes, file) == (size_t)data_bytes;
    written = fclose(file) == 0 && written;
    if (!written) {
        DeleteFileA(temporary);
        return 0;
    }
    DeleteFileA(path);
    return MoveFileA(temporary, path);
}

static int decode_to_wave(const char *source, int format, const char *destination) {
    int channels = 0, sample_rate = 0, written = 0;
    if (strcmp(music_formats[format].extension, "ogg") == 0) {
        short *samples = NULL;
        int frames = stb_vorbis_decode_filename(source, &channels, &sample_rate, &samples);
        if (frames > 0) written = write_wave_file(destination, samples, (uint64_t)frames, channels, sample_rate);
        free(samples);
    } else {
        unsigned int flac_channels = 0, flac_rate = 0;
        drflac_uint64 frames = 0;
        drflac_int16 *samples = drflac_open_file_and_read_pcm_frames_s16(source, &flac_channels, &flac_rate, &frames, NULL);
        if (samples && frames) written = write_wave_file(destination, samples, frames, (int)flac_channels, (int)flac_rate);
        channels = (int)flac_channels;
        sample_rate = (int)flac_rate;
        drflac_free(samples, NULL);
    }
    log_line("music: %s %s (%d channels, %d Hz)", written ? "decoded" : "cannot decode", source, channels, sample_rate);
    return written;
}

int music_decode_file(const char *source, const char *destination) {
    const char *extension = strrchr(source, '.');
    for (int i = 0; extension && i < MUSIC_FORMAT_COUNT; i++) {
        if (music_formats[i].kind == MUSIC_NEEDS_DECODING && _stricmp(extension + 1, music_formats[i].extension) == 0)
            return decode_to_wave(source, i, destination);
    }
    return 0;
}

static int cache_is_current(const char *source, const char *cache) {
    WIN32_FILE_ATTRIBUTE_DATA source_data, cache_data;
    if (!GetFileAttributesExA(source, GetFileExInfoStandard, &source_data)) return 0;
    if (!GetFileAttributesExA(cache, GetFileExInfoStandard, &cache_data)) return 0;
    return CompareFileTime(&cache_data.ftLastWriteTime, &source_data.ftLastWriteTime) >= 0;
}

static int cached_wave(const char *source, int format, const char *source_name, const char *track, char *cache,
                       size_t size) {
    snprintf(cache, size, "%s%s_%s.wav", cache_folder, source_name, track);
    EnterCriticalSection(&decode_lock);
    int ready = cache_is_current(source, cache);
    if (!ready) {
        CreateDirectoryA(music_folder, NULL);
        CreateDirectoryA(cache_folder, NULL);
        ready = decode_to_wave(source, format, cache);
    }
    LeaveCriticalSection(&decode_lock);
    return ready;
}

typedef struct {
    uint64_t start, end, length;
    int has_start, has_end, has_length;
} loop_tags;

static int parse_loop_value(const char *text, size_t length, int sample_rate, uint64_t *frames) {
    char value[48];
    size_t used = 0;
    while (length && (*text == ' ' || *text == '"')) text++, length--;
    while (used < length && used + 1 < sizeof value && strchr("0123456789:.", text[used])) {
        value[used] = text[used];
        used++;
    }
    value[used] = '\0';
    if (!used) return 0;
    if (!strchr(value, ':') && !strchr(value, '.')) {
        *frames = _strtoui64(value, NULL, 10);
        return 1;
    }
    double seconds = 0;
    for (char *field = strtok(value, ":"); field; field = strtok(NULL, ":")) seconds = seconds * 60.0 + atof(field);
    *frames = (uint64_t)(seconds * sample_rate + 0.5);
    return 1;
}

static void read_loop_tag(const char *text, size_t length, int sample_rate, loop_tags *tags) {
    static const char *const keys[] = {"LOOPSTART", "LOOPEND", "LOOPLENGTH"};
    for (int key = 0; key < 3; key++) {
        size_t key_length = strlen(keys[key]);
        if (length <= key_length || _strnicmp(text, keys[key], key_length)) continue;
        const char *value = text + key_length;
        size_t rest = length - key_length;
        while (rest && (*value == '=' || *value == '\0' || *value == ' ' || *value == '\x03' || *value == '\x01')) value++, rest--;
        uint64_t frames;
        if (!parse_loop_value(value, rest, sample_rate, &frames)) continue;
        if (key == 0) tags->start = frames, tags->has_start = 1;
        else if (key == 1) tags->end = frames, tags->has_end = 1;
        else tags->length = frames, tags->has_length = 1;
    }
}

static void scan_text_for_loop_tags(const uint8_t *data, size_t size, int sample_rate, loop_tags *tags) {
    for (size_t i = 0; i + 8 < size; i++) {
        if ((data[i] == 'L' || data[i] == 'l') && (data[i + 1] == 'O' || data[i + 1] == 'o')) {
            read_loop_tag((const char *)data + i, size - i < 96 ? size - i : 96, sample_rate, tags);
        }
    }
}

static void read_wave_loop_tags(const char *path, int sample_rate, loop_tags *tags) {
    FILE *file = fopen(path, "rb");
    if (!file) return;
    uint8_t header[12];
    if (fread(header, 1, 12, file) != 12 || memcmp(header, "RIFF", 4)) {
        fclose(file);
        return;
    }
    uint8_t chunk[8];
    while (fread(chunk, 1, 8, file) == 8) {
        uint32_t size;
        memcpy(&size, chunk + 4, 4);
        if (!memcmp(chunk, "data", 4) || size > 16 * 1024 * 1024) {
            fseek(file, (long)(size + (size & 1)), SEEK_CUR);
            continue;
        }
        uint8_t *body = malloc(size + 1);
        if (!body || fread(body, 1, size, file) != size) {
            free(body);
            break;
        }
        if (size & 1) fgetc(file);
        if (!memcmp(chunk, "smpl", 4) && size >= 36 + 24) {
            uint32_t loops, start, end;
            memcpy(&loops, body + 28, 4);
            memcpy(&start, body + 36 + 8, 4);
            memcpy(&end, body + 36 + 12, 4);
            if (loops && !tags->has_start) tags->start = start, tags->has_start = 1;
            if (loops && !tags->has_end) tags->end = (uint64_t)end + 1, tags->has_end = 1;
        } else {
            scan_text_for_loop_tags(body, size, sample_rate, tags);
        }
        free(body);
    }
    fclose(file);
}

static void read_ogg_loop_tags(const char *path, int sample_rate, loop_tags *tags) {
    int error = 0;
    stb_vorbis *vorbis = stb_vorbis_open_filename(path, &error, NULL);
    if (!vorbis) return;
    stb_vorbis_comment comments = stb_vorbis_get_comment(vorbis);
    for (int i = 0; i < comments.comment_list_length; i++) {
        read_loop_tag(comments.comment_list[i], strlen(comments.comment_list[i]), sample_rate, tags);
    }
    stb_vorbis_close(vorbis);
}

typedef struct {
    int sample_rate;
    loop_tags *tags;
} flac_tag_context;

static void on_flac_metadata(void *user, drflac_metadata *metadata) {
    flac_tag_context *context = user;
    if (metadata->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
    drflac_vorbis_comment_iterator iterator;
    drflac_init_vorbis_comment_iterator(&iterator, metadata->data.vorbis_comment.commentCount,
                                        metadata->data.vorbis_comment.pComments);
    drflac_uint32 length;
    const char *comment;
    while ((comment = drflac_next_vorbis_comment(&iterator, &length)) != NULL) {
        read_loop_tag(comment, length, context->sample_rate, context->tags);
    }
}

static void read_flac_loop_tags(const char *path, int sample_rate, loop_tags *tags) {
    flac_tag_context context = {sample_rate, tags};
    drflac *flac = drflac_open_file_with_metadata(path, on_flac_metadata, &context, NULL);
    if (flac) drflac_close(flac);
}

static void loop_points(const char *source, int format, const char *playable, uint64_t *start, uint64_t *end) {
    uint64_t frames = 0;
    int sample_rate = music_player_wave_rate(playable, &frames);
    loop_tags tags = {0};
    const char *extension = music_formats[format].extension;
    if (!strcmp(extension, "ogg")) read_ogg_loop_tags(source, sample_rate, &tags);
    else if (!strcmp(extension, "flac")) read_flac_loop_tags(source, sample_rate, &tags);
    else read_wave_loop_tags(source, sample_rate, &tags);
    *start = tags.has_start ? tags.start : 0;
    *end = tags.has_end ? tags.end : tags.has_length ? *start + tags.length : frames;
    if (*end > frames || *end <= *start) *end = frames;
    if (*start >= *end) *start = 0;
}

static char __cdecl play_track(const wchar_t *path, int loop) {
    char track[TRACK_NAME_LENGTH], playable[MAX_PATH], source[MAX_PATH], source_name[PACK_NAME_LENGTH];
    int format;
    music_player_stop();
    if (!path) return original_wave_play(path, loop);
    track_name_from_path(path, track, sizeof track);
    if (!find_replacement(track, source, sizeof source, &format, source_name, sizeof source_name)) {
        log_line("music: %s (game's own file)", track);
        return original_wave_play(path, loop);
    }
    int own_player = strcmp(music_formats[format].extension, "mp3") != 0;
    int ready = music_formats[format].kind == MUSIC_PLAYS_DIRECTLY
                    ? (lstrcpynA(playable, source, MAX_PATH), 1)
                    : cached_wave(source, format, source_name, track, playable, sizeof playable);
    if (ready && own_player) {
        uint64_t start, end;
        loop_points(source, format, playable, &start, &end);
        original_wave_stop();
        if (music_player_play(playable, loop, start, end)) {
            log_line("music: %s -> %s (%s, loop %llu-%llu)", track, source, loop ? "looping" : "once",
                     (unsigned long long)start, (unsigned long long)end);
            return 1;
        }
        log_line("music: %s: cannot stream %s, using DirectShow", track, playable);
    }
    if (!ready) return original_wave_play(path, loop);
    wchar_t wide[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, playable, -1, wide, MAX_PATH);
    char result = original_wave_play(wide, loop);
    log_line("music: %s -> %s (%s)", track, playable, result ? "playing" : "could not play, using the original");
    return result ? result : original_wave_play(path, loop);
}

static char __cdecl wave_play_hook(const wchar_t *path, int loop) {
    char result = play_track(path, loop);
    volume_apply_music();
    return result;
}

static char __cdecl wave_stop_hook(void) {
    if (music_player_active()) log_line("music: wave_stop from %p", __builtin_return_address(0));
    music_player_stop();
    return original_wave_stop();
}

static void prepare_folder(const char *folder, const char *source_name) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s*", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        char *extension = strrchr(found.cFileName, '.');
        if (!extension || (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        for (int format = 0; format < MUSIC_FORMAT_COUNT; format++) {
            if (music_formats[format].kind != MUSIC_NEEDS_DECODING || _stricmp(extension + 1, music_formats[format].extension)) continue;
            char source[MAX_PATH], track[TRACK_NAME_LENGTH], cache[MAX_PATH];
            snprintf(source, sizeof source, "%s%s", folder, found.cFileName);
            lstrcpynA(track, found.cFileName, (int)(extension - found.cFileName + 1));
            cached_wave(source, format, source_name, track, cache, sizeof cache);
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static DWORD WINAPI prepare_music_thread(LPVOID unused) {
    (void)unused;
    char folder[MAX_PATH];
    for (int pack = 0; pack < pack_count; pack++) {
        snprintf(folder, sizeof folder, "%s%s\\", music_folder, pack_names[pack]);
        prepare_folder(folder, pack_names[pack]);
    }
    snprintf(folder, sizeof folder, "%sSOUND\\BGM\\", game_folder_path);
    prepare_folder(folder, "BGM");
    return 0;
}

void install_music_hooks(const char *game_folder, const char *ini_path) {
    lstrcpynA(game_folder_path, game_folder, MAX_PATH);
    snprintf(music_folder, sizeof music_folder, "%smusic\\", game_folder);
    snprintf(cache_folder, sizeof cache_folder, "%s.cache\\", music_folder);
    read_pack_list(ini_path);
    InitializeCriticalSection(&decode_lock);
    for (int i = 0; i < pack_count; i++) log_line("music: pack %d = %s", i + 1, pack_names[i]);
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    void *target = hglcore ? (void *)GetProcAddress(hglcore, "wave_play") : NULL;
    if (!target) {
        log_line("music: HglCore wave_play not found");
        return;
    }
    MH_STATUS status = MH_CreateHook(target, wave_play_hook, (void **)&original_wave_play);
    if (status == MH_OK) status = MH_EnableHook(target);
    void *stop_target = (void *)GetProcAddress(hglcore, "wave_stop");
    MH_STATUS stop_status = stop_target ? MH_CreateHook(stop_target, wave_stop_hook, (void **)&original_wave_stop)
                                        : MH_ERROR_FUNCTION_NOT_FOUND;
    if (stop_status == MH_OK) stop_status = MH_EnableHook(stop_target);
    log_line("music: hooks wave_play %s, wave_stop %s (replacements: music\\<pack>\\ then SOUND\\BGM\\; wav, ogg, "
             "flac play with LOOPSTART/LOOPEND loops, mp3 through DirectShow)",
             MH_StatusToString(status), MH_StatusToString(stop_status));
    HANDLE thread = CreateThread(NULL, 0, prepare_music_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}
