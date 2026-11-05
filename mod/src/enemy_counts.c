#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "enemy_counts.h"
#include "log.h"
#include "frame_hooks.h"
#include "MinHook.h"

#define SPAWN_LIST_VA 0x004218d0
#define SPAWN_PARAM_LIST_VA 0x00421bb0
#define SPAWN_PARAM_LIST_IF_VA 0x00421c70
#define SCRIPT_POINTER (*(int ***)0x00a195a0)
#define GAME_FRAME (*(volatile uint32_t *)0x00a18fac)
#define WORD_ATTRIBUTE(type) (*(const uint32_t *)(0x005262c0 + (type) * 4))
#define LIST_END ((int *)-1)
#define RECORD_INTS 10
#define PARAM_RECORD_INTS 128
#define RECORD_KIND(record) ((int)(signed char)(record)[9])
#define LAST_ZOMBIE_KIND 25
#define ASSASSIN_JUMPER_KIND 1
#define NINJA_JUMPER_KIND 13
#define ASSASSIN2_JUMPER_KIND 22
#define MONKEY_JUMPER_KIND 25
#define FLAT_BEHAVIOUR_BYTE 0x25
#define PARAM_BEHAVIOUR_BYTE 0x26
#define WALK_IN_BEHAVIOUR 0x01
#define APPROACH_BEHAVIOUR 0x12
#define THROWER_BEHAVIOUR 0x21
#define MAX_COPIES 4
#define MAX_LIST 48
#define MAX_EXTRA_PER_LIST 12
#define COPY_POOL 256
#define MIN_OFFSET 0.3f
#define MAX_OFFSET 0.8f
#define LOGGED_LISTS 400

typedef void(__cdecl *spawn_list_fn)(void);
typedef char *(__cdecl *spawn_record_fn)(int *record);
static spawn_record_fn original_spawn_record;
static int spawning_list;
#define WATCHED 64
static struct {
    char *object;
    int copy;
    uint32_t frame;
} watched[WATCHED];
static int watched_count;
static spawn_list_fn original_spawn_list, original_spawn_param_list, original_spawn_param_list_if;
static int copies_min = 1, copies_max = 1;
static uint32_t base_seed;
static int record_copies[COPY_POOL][PARAM_RECORD_INTS];
static int next_copy;
static int logged_lists = LOGGED_LISTS;
static long extra_total, removed_total;

static uint32_t mix_bits(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static int behaviour_copies_safely(const int *record, int record_ints) {
    const unsigned char *bytes = (const unsigned char *)record;
    int behaviour = record_ints == RECORD_INTS ? bytes[FLAT_BEHAVIOUR_BYTE] : bytes[PARAM_BEHAVIOUR_BYTE];
    return behaviour == WALK_IN_BEHAVIOUR || behaviour == APPROACH_BEHAVIOUR || behaviour == THROWER_BEHAVIOUR;
}

static int is_multipliable(const int *record, int record_ints) {
    int kind = RECORD_KIND(record);
    if (!behaviour_copies_safely(record, record_ints)) return 0;
    if (kind == ASSASSIN_JUMPER_KIND || kind == NINJA_JUMPER_KIND || kind == ASSASSIN2_JUMPER_KIND ||
        kind == MONKEY_JUMPER_KIND)
        return 0;
    return kind >= 0 && kind <= LAST_ZOMBIE_KIND && WORD_ATTRIBUTE(record[0]) != 0;
}

static int is_copy_record(const int *record) {
    return (const char *)record >= (const char *)record_copies && (const char *)record < (const char *)(record_copies + COPY_POOL);
}

static char *__cdecl spawn_record_hook(int *record) {
    char *object = original_spawn_record(record);
    if (spawning_list && object) {
        watched[watched_count % WATCHED].object = object;
        watched[watched_count % WATCHED].copy = is_copy_record(record);
        watched[watched_count % WATCHED].frame = GAME_FRAME;
        watched_count++;
    }
    return object;
}

typedef void(__cdecl *body_init_fn)(char *animation, void *position, void *extra);
static body_init_fn original_body_init;
static int body_logs;

static void __cdecl body_init_hook(char *animation, void *position, void *extra) {
    if (body_logs++ < 2000) {
        float *at = (float *)position;
        log_line("enemy counts: frame %u body kind %d task %p from %p at %.1f %.1f %.1f", GAME_FRAME,
                 *(short *)(animation + 0x60), *(void **)(animation - 0x198), __builtin_return_address(0), at[0], at[1],
                 at[2]);
    }
    original_body_init(animation, position, extra);
}

static void log_enemy_counters(void) {
    static int last_a, last_b;
    int a = *(volatile short *)0x00a18618, b = *(volatile short *)0x00a18fb0;
    uint32_t frame = GAME_FRAME;
    if (a == last_a && b == last_b && frame % 600) return;
    last_a = a;
    last_b = b;
    log_line("enemy counts: frame %u counters %d %d", frame, a, b);
    if (frame % 600) return;
    for (int i = 0; i < watched_count && i < WATCHED; i++) {
        char *object = watched[i].object;
        float x, y, z;
        memcpy(&x, object + 0x40, 4);
        memcpy(&y, object + 0x44, 4);
        memcpy(&z, object + 0x48, 4);
        log_line("  %s spawned %u task %p beh %d at %.1f %.1f %.1f", watched[i].copy ? "copy" : "orig", watched[i].frame,
                 *(void **)object, *(int *)(object + 0x1310), x, y, z);
    }
}

static int *place_copy(const int *record, int ints, uint32_t random) {
    int *copy = record_copies[next_copy++ % COPY_POOL];
    while (ints > RECORD_INTS && IsBadReadPtr(record, (UINT_PTR)ints * 4)) ints /= 2;
    memcpy(copy, record, (size_t)ints * 4);
    float distance_x = MIN_OFFSET + (float)(random & 0xff) / 255.0f * (MAX_OFFSET - MIN_OFFSET);
    float distance_z = MIN_OFFSET + (float)((random >> 8) & 0xff) / 255.0f * (MAX_OFFSET - MIN_OFFSET);
    float x, z;
    memcpy(&x, &copy[2], sizeof x);
    memcpy(&z, &copy[4], sizeof z);
    x += random & 0x10000 ? distance_x : -distance_x;
    z += random & 0x20000 ? distance_z * 0.5f : -distance_z * 0.5f;
    memcpy(&copy[2], &x, sizeof x);
    memcpy(&copy[4], &z, sizeof z);
    return copy;
}

static void log_record(const int *record, int copies) {
    float x, y, z;
    memcpy(&x, &record[2], sizeof x);
    memcpy(&y, &record[3], sizeof y);
    memcpy(&z, &record[4], sizeof z);
    const unsigned char *params = (const unsigned char *)record + 0x24;
    log_line("enemy counts: frame %u type %d flags %x kind %d hp %d params %02x %02x %02x %02x at %.1f %.1f %.1f -> %d",
             GAME_FRAME, record[0], record[1], RECORD_KIND(record), *(const short *)((const char *)record + 0x22),
             params[0], params[1], params[2], params[3], x, y, z, copies);
}

static void spawn_multiplied(spawn_list_fn original, int record_ints) {
    int **script = SCRIPT_POINTER;
    int *records[MAX_LIST];
    int count = 0;
    while (script[1 + count] != LIST_END && count < MAX_LIST) {
        records[count] = script[1 + count];
        count++;
    }
    if (count == MAX_LIST || script[1 + count] != LIST_END) {
        original();
        return;
    }
    int *list[1 + MAX_LIST + MAX_EXTRA_PER_LIST + 1];
    int used = 1, extra = 0, kept_multipliable = 0, skipped = 0;
    uint32_t frame = GAME_FRAME;
    for (int i = 0; i < count; i++) {
        int *record = records[i];
        if (!is_multipliable(record, record_ints)) {
            list[used++] = record;
            continue;
        }
        uint32_t random = mix_bits(base_seed ^ mix_bits(frame * 64u + (uint32_t)i));
        int span = copies_max - copies_min + 1;
        int copies = copies_min + (int)(random % (uint32_t)span);
        int last_chance = i == count - 1 && !kept_multipliable;
        if (copies == 0 && last_chance) copies = 1;
        if (logged_lists < LOGGED_LISTS) log_record(record, copies);
        if (copies == 0) {
            skipped++;
            continue;
        }
        kept_multipliable++;
        list[used++] = record;
        for (int c = 1; c < copies && extra < MAX_EXTRA_PER_LIST; c++, extra++) {
            list[used++] = place_copy(record, record_ints, mix_bits(random + (uint32_t)c * 0x9e3779b9U));
        }
    }
    list[used] = LIST_END;
    if (logged_lists < LOGGED_LISTS) logged_lists++;
    extra_total += extra;
    removed_total += skipped;
    SCRIPT_POINTER = (int **)list;
    spawning_list = 1;
    original();
    spawning_list = 0;
    SCRIPT_POINTER = script + count + 2;
}

static void __cdecl spawn_list_hook(void) {
    spawn_multiplied(original_spawn_list, RECORD_INTS);
}

static void __cdecl spawn_param_list_hook(void) {
    spawn_multiplied(original_spawn_param_list, PARAM_RECORD_INTS);
}

static void __cdecl spawn_param_list_if_hook(void) {
    spawn_multiplied(original_spawn_param_list_if, PARAM_RECORD_INTS);
}

static const char *hook_spawn(uintptr_t address, void *hook, spawn_list_fn *original) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, (void **)original);
    if (status == MH_OK) status = MH_EnableHook((void *)address);
    return MH_StatusToString(status);
}

void install_enemy_counts(const char *ini_path, uint32_t seed) {
    copies_min = (int)GetPrivateProfileIntA("randomizer", "enemy_min", 1, ini_path);
    copies_max = (int)GetPrivateProfileIntA("randomizer", "enemy_max", 1, ini_path);
    if (copies_min < 0) copies_min = 0;
    if (copies_min > MAX_COPIES) copies_min = MAX_COPIES;
    if (copies_max < copies_min) copies_max = copies_min;
    if (copies_max > MAX_COPIES) copies_max = MAX_COPIES;
    int log_enemies = GetPrivateProfileIntA("debug", "log_enemies", 0, ini_path);
    if (copies_min == 1 && copies_max == 1 && !log_enemies) return;
    base_seed = mix_bits(seed ^ 0x51ed270bU);
    const char *list = hook_spawn(SPAWN_LIST_VA, spawn_list_hook, &original_spawn_list);
    const char *param_list = hook_spawn(SPAWN_PARAM_LIST_VA, spawn_param_list_hook, &original_spawn_param_list);
    const char *param_list_if = hook_spawn(SPAWN_PARAM_LIST_IF_VA, spawn_param_list_if_hook, &original_spawn_param_list_if);
    if (log_enemies) {
        logged_lists = 0;
        hook_spawn(0x00421ad0, spawn_record_hook, (spawn_list_fn *)&original_spawn_record);
        frame_hooks_on_frame_end(log_enemy_counters);
        hook_spawn(0x004789c0, body_init_hook, (spawn_list_fn *)&original_body_init);
    }
    log_line("randomizer: %d-%d of each zombie (hooks spawn lists: %s %s %s)", copies_min, copies_max, list, param_list,
             param_list_if);
}

void enemy_counts_finish(void) {
    if (extra_total || removed_total) log_line("enemy counts: %ld extra zombies, %ld left out", extra_total, removed_total);
}
