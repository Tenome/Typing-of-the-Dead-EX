#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "replay.h"
#include "determinism.h"
#include "online_fingerprint.h"
#include "online_hooks.h"
#include "log.h"

#define REPLAY_MAGIC "TODRPLY1"
#define REPLAY_VERSION 1
#define FRAME_BYTES (REPORT_SIZE * 2)
#define CHECK_INTERVAL 60
#define INITIAL_FRAMES 65536
#define FLUSH_FRAMES 3600
#define MAX_RUN 0xffff
#define MAX_REPORTED_DRIFTS 10
#define DEFAULT_KEEP 20

typedef struct {
    char magic[8];
    uint32_t version;
    char todmod_version[16];
    uint32_t fingerprint[FINGERPRINT_PARTS];
    uint32_t randomizer_seed;
    uint32_t has_randomizer_seed;
    uint32_t save_size;
    uint32_t frame_count;
    uint32_t check_interval;
    uint32_t check_count;
    uint32_t reserved[4];
} replay_header;

typedef struct {
    uint32_t frame;
    uint32_t hash[DETERMINISM_HASH_PARTS];
} replay_check;

enum { REPLAY_OFF, REPLAY_RECORD, REPLAY_PLAY };

static int mode = REPLAY_OFF;
static replay_header header;
static unsigned char *save_copy;
static unsigned char *frames;
static uint32_t frame_capacity;
static replay_check *checks;
static uint32_t check_capacity;
static char replay_path[MAX_PATH];
static uint32_t next_check;
static int drifts;
static int playback_done;
static uint32_t last_flush;
static char settings_path[MAX_PATH];

int replay_active(void) {
    return mode != REPLAY_OFF;
}

int replay_playing(void) {
    return mode == REPLAY_PLAY && !playback_done;
}

static int grow(void **buffer, uint32_t *capacity, uint32_t needed, size_t item) {
    if (needed <= *capacity) return 1;
    uint32_t bigger = *capacity ? *capacity * 2 : INITIAL_FRAMES;
    while (bigger < needed) bigger *= 2;
    void *larger = realloc(*buffer, bigger * item);
    if (!larger) return 0;
    *buffer = larger;
    *capacity = bigger;
    return 1;
}

static void write_replay_file(void) {
    FILE *file = fopen(replay_path, "wb");
    if (!file) return;
    fwrite(&header, sizeof header, 1, file);
    if (header.save_size) fwrite(save_copy, 1, header.save_size, file);
    for (uint32_t i = 0; i < header.frame_count;) {
        uint32_t run = 1;
        const unsigned char *frame = frames + (size_t)i * FRAME_BYTES;
        while (i + run < header.frame_count && run < MAX_RUN && !memcmp(frame, frame + (size_t)run * FRAME_BYTES, FRAME_BYTES)) run++;
        uint16_t run16 = (uint16_t)run;
        fwrite(&run16, sizeof run16, 1, file);
        fwrite(frame, 1, FRAME_BYTES, file);
        i += run;
    }
    if (header.check_count) fwrite(checks, sizeof *checks, header.check_count, file);
    fclose(file);
}

static int read_replay_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fread(&header, sizeof header, 1, file) == 1 && !memcmp(header.magic, REPLAY_MAGIC, 8) &&
             header.version == REPLAY_VERSION && header.save_size <= SAVE_SIZE;
    if (ok && header.save_size) {
        save_copy = malloc(SAVE_SIZE);
        ok = save_copy && fread(save_copy, 1, header.save_size, file) == header.save_size;
    }
    if (ok) ok = grow((void **)&frames, &frame_capacity, header.frame_count + 1, FRAME_BYTES);
    for (uint32_t i = 0; ok && i < header.frame_count;) {
        uint16_t run;
        unsigned char frame[FRAME_BYTES];
        ok = fread(&run, sizeof run, 1, file) == 1 && fread(frame, 1, FRAME_BYTES, file) == FRAME_BYTES && run &&
             i + run <= header.frame_count;
        for (uint32_t j = 0; ok && j < run; j++) memcpy(frames + (size_t)(i + j) * FRAME_BYTES, frame, FRAME_BYTES);
        i += run;
    }
    if (ok && header.check_count) {
        ok = grow((void **)&checks, &check_capacity, header.check_count, sizeof *checks) &&
             fread(checks, sizeof *checks, header.check_count, file) == header.check_count;
    }
    fclose(file);
    return ok;
}

static void record_input(void) {
    uint32_t frame = header.frame_count;
    if (!grow((void **)&frames, &frame_capacity, frame + 1, FRAME_BYTES)) return;
    memcpy(frames + (size_t)frame * FRAME_BYTES, determinism_device_report(0), REPORT_SIZE);
    memcpy(frames + (size_t)frame * FRAME_BYTES + REPORT_SIZE, determinism_device_report(1), REPORT_SIZE);
    if (frame % CHECK_INTERVAL == 0 && grow((void **)&checks, &check_capacity, header.check_count + 1, sizeof *checks)) {
        checks[header.check_count].frame = frame;
        determinism_hash(checks[header.check_count].hash);
        header.check_count++;
    }
    header.frame_count++;
    if (header.frame_count - last_flush >= FLUSH_FRAMES) {
        write_replay_file();
        last_flush = header.frame_count;
    }
}

static void play_input(void) {
    uint32_t frame = determinism_input_frame();
    if (playback_done) return;
    if (frame >= header.frame_count) {
        playback_done = 1;
        log_line("replay: finished after %u frames (%u drift warnings); the keyboard works again", frame, drifts);
        return;
    }
    while (next_check < header.check_count && checks[next_check].frame < frame) next_check++;
    if (next_check < header.check_count && checks[next_check].frame == frame) {
        uint32_t now[DETERMINISM_HASH_PARTS];
        determinism_hash(now);
        if (memcmp(now, checks[next_check].hash, sizeof now) && drifts++ < MAX_REPORTED_DRIFTS)
            log_line("replay: game differs from the recording at frame %u (recorded %u/%u, now %u/%u)", frame,
                     checks[next_check].hash[0], checks[next_check].hash[1], now[0], now[1]);
        next_check++;
    }
    memcpy(determinism_device_report(0), frames + (size_t)frame * FRAME_BYTES, REPORT_SIZE);
    memcpy(determinism_device_report(1), frames + (size_t)frame * FRAME_BYTES + REPORT_SIZE, REPORT_SIZE);
}

static int load_recorded_save(unsigned char *buffer) {
    if (!header.save_size) return 0;
    memcpy(buffer, save_copy, header.save_size);
    log_line("replay: the game loads the save stored in the replay");
    return 1;
}

static void remember_loaded_save(const unsigned char *buffer, int loaded) {
    if (header.save_size || !loaded) return;
    save_copy = malloc(SAVE_SIZE);
    if (!save_copy) return;
    memcpy(save_copy, buffer, SAVE_SIZE);
    header.save_size = SAVE_SIZE;
}

static void remove_old_replays(const char *folder, int keep) {
    char pattern[MAX_PATH], names[256][64];
    int count = 0;
    snprintf(pattern, sizeof pattern, "%s*.todreplay", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (count < 256 && strlen(found.cFileName) < 64) strcpy(names[count++], found.cFileName);
    } while (FindNextFileA(search, &found));
    FindClose(search);
    qsort(names, (size_t)count, sizeof names[0], (int (*)(const void *, const void *))strcmp);
    for (int i = 0; i + keep < count; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s%s", folder, names[i]);
        DeleteFileA(path);
    }
}

void replay_prepare(const char *game_folder, const char *ini_path, const char *version) {
    char play[MAX_PATH] = "";
    lstrcpynA(settings_path, ini_path, MAX_PATH);
    if (!GetEnvironmentVariableA("TODMOD_REPLAY", play, sizeof play))
        GetPrivateProfileStringA("replay", "play", "", play, sizeof play, ini_path);
    int record = GetPrivateProfileIntA("replay", "record", 1, ini_path);
    if (online_role_from_settings(ini_path) && (play[0] || record)) {
        if (play[0]) log_line("replay: not played back during online play");
        play[0] = '\0';
    }
    uint32_t fingerprint[FINGERPRINT_PARTS];
    online_fingerprint(ini_path, version, fingerprint);
    if (play[0]) {
        if (strchr(play, '\\') || strchr(play, ':')) lstrcpynA(replay_path, play, MAX_PATH);
        else snprintf(replay_path, sizeof replay_path, "%sreplays\\%s", game_folder, play);
        if (!read_replay_file(replay_path)) {
            log_line("replay: cannot read %s; playing normally", replay_path);
            return;
        }
        mode = REPLAY_PLAY;
        log_line("replay: playing %s (%u frames, about %u s, recorded with todmod %s)", replay_path, header.frame_count,
                 header.frame_count / 60, header.todmod_version);
        for (int i = 0; i < FINGERPRINT_PARTS; i++) {
            if (fingerprint[i] != header.fingerprint[i])
                log_line("replay: recorded with different %s; the replay may drift", online_fingerprint_part_name(i));
        }
        return;
    }
    if (!record) return;
    char folder[MAX_PATH];
    snprintf(folder, sizeof folder, "%sreplays\\", game_folder);
    CreateDirectoryA(folder, NULL);
    remove_old_replays(folder, GetPrivateProfileIntA("replay", "keep", DEFAULT_KEEP, ini_path) - 1);
    SYSTEMTIME now;
    GetLocalTime(&now);
    snprintf(replay_path, sizeof replay_path, "%s%04d-%02d-%02d_%02d%02d%02d.todreplay", folder, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);
    memcpy(header.magic, REPLAY_MAGIC, 8);
    header.version = REPLAY_VERSION;
    lstrcpynA(header.todmod_version, version, sizeof header.todmod_version);
    memcpy(header.fingerprint, fingerprint, sizeof fingerprint);
    header.check_interval = CHECK_INTERVAL;
    mode = REPLAY_RECORD;
}

int replay_forced_randomizer_seed(uint32_t *seed) {
    if (mode != REPLAY_PLAY || !header.has_randomizer_seed) return 0;
    *seed = header.randomizer_seed;
    return 1;
}

void replay_note_randomizer_seed(uint32_t seed) {
    if (mode != REPLAY_RECORD) return;
    header.randomizer_seed = seed;
    header.has_randomizer_seed = 1;
}

void install_replay(void) {
    if (mode == REPLAY_OFF) return;
    install_determinism(settings_path, mode == REPLAY_PLAY ? "replay" : "record");
    if (mode == REPLAY_PLAY) {
        determinism_on_input(play_input);
        determinism_set_save_loader(load_recorded_save);
        determinism_block_save_writes();
    } else {
        determinism_on_input(record_input);
        determinism_on_save_loaded(remember_loaded_save);
        log_line("replay: recording this game to %s", replay_path);
    }
}

void replay_finish(void) {
    if (mode != REPLAY_RECORD || !header.frame_count) return;
    write_replay_file();
    log_line("replay: saved %u frames (%u s) to %s", header.frame_count, header.frame_count / 60, replay_path);
}
