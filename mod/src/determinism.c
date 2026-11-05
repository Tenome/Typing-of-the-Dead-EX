#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "determinism.h"
#include "log.h"
#include "MinHook.h"

#define READ_INPUT_VA 0x0049b090
#define LOAD_SAVE_VA 0x004c4b10
#define WRITE_SAVE_VA 0x004c4bc0
#define SAVE_BUFFER ((unsigned char *)0x00a1b760)
#define SAVE_FOLDER ((const char *)0x00547ef8)
#define SAVE_NAME "user.bin"
#define DEVICE_REPORTS 0x0077a1ac
#define GAME_FRAME (*(volatile uint32_t *)0x00a18fac)
#define GAME_MODE (*(volatile uint32_t *)0x00a3e308)
#define GAME_SUBMODE (*(volatile uint32_t *)0x00a3e248)
#define PADS_HELD (*(volatile uint32_t *)0x00a3e780)
#define PADS_CHANGED (*(volatile uint32_t *)0x00a3e788)
#define ITEM_EFFECTS (*(volatile uint32_t *)0x009f0be0)
#define HGLNA_RAND_OFFSET 0x49a0
#define HGLNA_SEED_OFFSET 0x49b0
#define MAX_INPUT_CALLBACKS 4

typedef void(__cdecl *void_fn)(void);
typedef int(__cdecl *int_fn)(void);
typedef void(__cdecl *seed_fn)(unsigned seed);

static void_fn original_read_input;
static int_fn original_rand;
static seed_fn original_seed;
static int_fn original_load_save;
static int_fn original_write_save;

static int installed;
static input_callback input_callbacks[MAX_INPUT_CALLBACKS];
static int input_callback_count;
static save_loader loader;
static save_observer observer;
static int block_writes, write_skip_logged;
static uint32_t input_frame;
static uint32_t rand_calls, last_rand, last_seed;
static FILE *rand_log;
static DWORD game_thread;

unsigned char *determinism_device_report(int device) {
    return (unsigned char *)(DEVICE_REPORTS + device * REPORT_SIZE);
}

uint32_t determinism_input_frame(void) {
    return input_frame;
}

unsigned determinism_game_thread(void) {
    return game_thread;
}

int determinism_installed(void) {
    return installed;
}

void determinism_on_input(input_callback callback) {
    if (input_callback_count < MAX_INPUT_CALLBACKS) input_callbacks[input_callback_count++] = callback;
}

void determinism_set_save_loader(save_loader new_loader) {
    loader = new_loader;
}

void determinism_on_save_loaded(save_observer new_observer) {
    observer = new_observer;
}

void determinism_block_save_writes(void) {
    block_writes = 1;
}

void determinism_hash(uint32_t parts[DETERMINISM_HASH_PARTS]) {
    parts[0] = GAME_FRAME;
    parts[1] = rand_calls;
    parts[2] = last_rand ^ (last_seed << 16);
    parts[3] = GAME_MODE * 0x01000193u ^ GAME_SUBMODE * 0x9e3779b1u ^ PADS_HELD * 0x85ebca6bu ^
               PADS_CHANGED * 0xc2b2ae35u ^ ITEM_EFFECTS;
}

int determinism_read_save_file(unsigned char *buffer) {
    char path[MAX_PATH];
    _snprintf(path, sizeof path, "%s%s", SAVE_FOLDER, SAVE_NAME);
    path[sizeof path - 1] = '\0';
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int size = (int)fread(buffer, 1, SAVE_SIZE, file);
    fclose(file);
    return size;
}

static int __cdecl rand_hook(void) {
    int value = original_rand();
    rand_calls++;
    last_rand = (uint32_t)value;
    if (rand_log)
        fprintf(rand_log, "%u\t%u\t%p\t%d\n", input_frame, rand_calls, __builtin_return_address(0),
                GetCurrentThreadId() == game_thread);
    return value;
}

static void __cdecl seed_hook(unsigned seed) {
    last_seed = seed;
    original_seed(seed);
}

static void __cdecl read_input_hook(void) {
    original_read_input();
    game_thread = GetCurrentThreadId();
    for (int i = 0; i < input_callback_count; i++) input_callbacks[i]();
    input_frame++;
}

static int __cdecl load_save_hook(void) {
    game_thread = GetCurrentThreadId();
    int result = loader ? loader(SAVE_BUFFER) : -1;
    if (result < 0) result = original_load_save();
    if (observer) observer(SAVE_BUFFER, result);
    return result;
}

static int __cdecl write_save_hook(void) {
    if (!block_writes) return original_write_save();
    if (!write_skip_logged) {
        write_skip_logged = 1;
        log_line("determinism: user.bin is left untouched (this game ran on a save from elsewhere)");
    }
    return 1;
}

static MH_STATUS hook_address(void *address, void *hook, void **original) {
    if (!address) return MH_ERROR_FUNCTION_NOT_FOUND;
    MH_STATUS status = MH_CreateHook(address, hook, original);
    return status == MH_OK ? MH_EnableHook(address) : status;
}

void install_determinism(const char *ini_path, const char *log_suffix) {
    if (installed) return;
    installed = 1;
    if (GetPrivateProfileIntA("online", "log_rand_callers", 0, ini_path)) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char *slash = strrchr(path, '\\');
        char *name = slash ? slash + 1 : path;
        _snprintf(name, MAX_PATH - (name - path), "todmod_rand_%s.tsv", log_suffix);
        path[MAX_PATH - 1] = '\0';
        rand_log = fopen(path, "w");
    }
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    log_line("determinism: hooks read_input %s, nlRand %s, nlRandomSeed %s, save load %s, save write %s",
             MH_StatusToString(hook_address((void *)READ_INPUT_VA, read_input_hook, (void **)&original_read_input)),
             MH_StatusToString(hook_address(hglna ? (char *)hglna + HGLNA_RAND_OFFSET : NULL, rand_hook, (void **)&original_rand)),
             MH_StatusToString(hook_address(hglna ? (char *)hglna + HGLNA_SEED_OFFSET : NULL, seed_hook, (void **)&original_seed)),
             MH_StatusToString(hook_address((void *)LOAD_SAVE_VA, load_save_hook, (void **)&original_load_save)),
             MH_StatusToString(hook_address((void *)WRITE_SAVE_VA, write_save_hook, (void **)&original_write_save)));
}
