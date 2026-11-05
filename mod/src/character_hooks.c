#include <windows.h>
#include <stdint.h>
#include "character_hooks.h"
#include "log.h"
#include "MinHook.h"
#include "overlay_hooks.h"
#include "frame_hooks.h"
#include <stdlib.h>
#include <string.h>

#define PREPARE_EVENT_CHARACTER_VA 0x0047b3f0
#define INIT_SCRIPTED_CHARACTER_VA 0x0047ea70
#define GAMEPLAY_PLAYER_KINDS ((volatile short *)0x0052c800)
#define GAMEPLAY_PLAYER_KIND_STRIDE 2
#define SCRIPT_ENTRY_OFFSET 0x1394
#define EVENT_ENTRY_CODE_OFFSET 4
#define EVENT_CHARACTER_KIND_OFFSET 0x60
#define FIRST_PLAYER_CODE 0x39
#define GAME_TYPE (*(volatile int *)0x00a3e248)
#define STAFF_CREDITS_GAME_TYPE 7
#define FIRST_PLAYER_BANK 0xc4
#define PLAYERS 2
#define BODY_NODE_CHILD_COUNT 0x16
#define BODY_NODE_CHILDREN 0x18
#define MAX_BODY_DEPTH 32
#define MAX_KINDS 128
#define NORMAL (-1)
#define MODEL_ID_LIMIT 0x24b3
#define NO_BANK 0x102
#define MAX_KIND_BANKS 8
#define MAX_SCRIPT_KIND 127
#define PLAYER_KIND_FIRST 57
#define TORSO_PART_ID 1
#define CHARACTER_BODY_OFFSET 0x198
#define BODY_PARTS_OFFSET 0x78
#define CURRENT_BODY_PARTS (*(char *volatile *)0x00a3e8dc)
#define GAMEPLAY_PLAYER_OBJECTS ((char *volatile *)0x00a18688)
#define MODEL_TABLE ((volatile model_slot *)0x009f3ac0)
#define MAX_PLAYER_BODIES 16
#define OBJECT_HEADER_WORDS 6
#define CHUNK_HEADER_WORDS 20
#define CHUNK_TEXTURE_WORD 8
#define CHUNK_SIZE_WORD 19
#define VERTEX_WORDS 8
#define STRIP_LIST_FLAG 0x08
#define STRIP_REFERENCES_FLAG 0x20
#define TEXTURE_ID_MASK 0x3FFFFFFF
#define MAX_OBJECT_BYTES 0x100000

typedef struct {
    void *buffer;
    void *object;
    short previous, next;
    unsigned short flags;
    short unused;
} model_slot;

typedef int(__cdecl *prepare_event_character_fn)(char *entry, char *character);
typedef void(__cdecl *init_scripted_character_fn)(char *character);
static prepare_event_character_fn original_prepare_event_character;
static init_scripted_character_fn original_init_scripted_character;

static int player_kind[PLAYERS] = {NORMAL, NORMAL};
static int player_banks[PLAYERS][MAX_KIND_BANKS];
static int player_bank_count[PLAYERS];
static int custom_torso_model[PLAYERS];
static int player_torso_model[PLAYERS];
static uint32_t *equipment_objects[PLAYERS];
static const void *equipment_source[PLAYERS];
static char *player_bodies[MAX_PLAYER_BODIES];
static int player_body_owner[MAX_PLAYER_BODIES];
static int next_player_body;
static int carry_equipment;

static int player_for_code(int code) {
    int player = code - FIRST_PLAYER_CODE;
    return player >= 0 && player < PLAYERS && player_kind[player] != NORMAL ? player : -1;
}

static int is_override_bank(int bank) {
    for (int player = 0; player < PLAYERS; player++) {
        for (int i = 0; i < player_bank_count[player]; i++) {
            if (player_banks[player][i] == bank) return 1;
        }
    }
    return 0;
}

static void remember_player_body(char *body, int player) {
    for (int i = 0; i < MAX_PLAYER_BODIES; i++) {
        if (player_bodies[i] == body) {
            player_body_owner[i] = player;
            return;
        }
    }
    player_bodies[next_player_body] = body;
    player_body_owner[next_player_body] = player;
    next_player_body = (next_player_body + 1) % MAX_PLAYER_BODIES;
}

static int player_of_body(const char *body) {
    for (int player = 0; player < PLAYERS; player++) {
        char *gameplay = GAMEPLAY_PLAYER_OBJECTS[player];
        if (gameplay && gameplay + CHARACTER_BODY_OFFSET == body) return player;
    }
    for (int i = 0; i < MAX_PLAYER_BODIES; i++) {
        if (player_bodies[i] == body) return player_body_owner[i];
    }
    return -1;
}

static int __cdecl prepare_event_character_hook(char *entry, char *character) {
    int result = original_prepare_event_character(entry, character);
    if (GAME_TYPE == STAFF_CREDITS_GAME_TYPE) return result;
    int player = entry ? player_for_code(entry[EVENT_ENTRY_CODE_OFFSET]) : -1;
    if (player < 0) return result;
    *(short *)(character + EVENT_CHARACTER_KIND_OFFSET) = (short)player_kind[player];
    remember_player_body(character, player);
    return 1;
}

static void __cdecl init_scripted_character_hook(char *character) {
    char *entry = *(char **)(character + SCRIPT_ENTRY_OFFSET);
    int player = entry && GAME_TYPE != STAFF_CREDITS_GAME_TYPE ? player_for_code(entry[0]) : -1;
    if (player < 0 || player_kind[player] > MAX_SCRIPT_KIND) {
        original_init_scripted_character(character);
        return;
    }
    remember_player_body(character + CHARACTER_BODY_OFFSET, player);
    char code = entry[0];
    entry[0] = (char)player_kind[player];
    original_init_scripted_character(character);
    entry[0] = code;
}

static int is_equipment_texture(uint32_t texture_id) {
    return (texture_id >= 5363 && texture_id <= 5366) || (texture_id >= 5512 && texture_id <= 5596) ||
           (texture_id >= 5752 && texture_id <= 5756);
}

static size_t object_bytes(const uint32_t *object) {
    const uint32_t *cursor = object + OBJECT_HEADER_WORDS;
    const uint32_t *limit = object + MAX_OBJECT_BYTES / 4;
    while (cursor < limit && *cursor) {
        if (*cursor >> 31) {
            cursor += CHUNK_HEADER_WORDS;
            continue;
        }
        uint32_t flags = cursor[0], count = cursor[1];
        if (flags & STRIP_LIST_FLAG) count *= 3;
        cursor += 2;
        for (uint32_t i = 0; i < count && cursor < limit; i++) {
            cursor += (flags & STRIP_REFERENCES_FLAG) && !(cursor[0] & 1) ? 2 : VERTEX_WORDS;
        }
    }
    return cursor < limit ? (size_t)(cursor + 1 - object) * 4 : 0;
}

static void collapse_strips(uint32_t *cursor, uint32_t *chunk_end) {
    while (cursor < chunk_end && *cursor && !(*cursor >> 31)) {
        uint32_t flags = cursor[0], count = cursor[1];
        if (flags & STRIP_LIST_FLAG) count *= 3;
        cursor += 2;
        for (uint32_t i = 0; i < count && cursor < chunk_end; i++) {
            if ((flags & STRIP_REFERENCES_FLAG) && !(cursor[0] & 1)) {
                cursor += 2;
                continue;
            }
            cursor[0] = 1;
            cursor[1] = cursor[2] = 0;
            cursor += VERTEX_WORDS;
        }
    }
}

static uint32_t *build_equipment_object(const uint32_t *torso) {
    size_t size = object_bytes(torso);
    uint32_t *copy = size ? malloc(size) : NULL;
    if (!copy) return NULL;
    memcpy(copy, torso, size);
    uint32_t *cursor = copy + OBJECT_HEADER_WORDS;
    int kept = 0;
    while (*cursor >> 31) {
        uint32_t *strips = cursor + CHUNK_HEADER_WORDS;
        uint32_t *next = strips + (cursor[CHUNK_SIZE_WORD] >> 2);
        if (is_equipment_texture(cursor[CHUNK_TEXTURE_WORD] & TEXTURE_ID_MASK)) kept++;
        else collapse_strips(strips, next);
        cursor = next;
    }
    if (!kept) {
        free(copy);
        return NULL;
    }
    return copy;
}

static void *__cdecl equipment_for_drawn_object(void *object) {
    for (int player = 0; player < PLAYERS; player++) {
        if (player_kind[player] == NORMAL || !custom_torso_model[player]) continue;
        if (object != MODEL_TABLE[custom_torso_model[player]].object) continue;
        char *parts = CURRENT_BODY_PARTS;
        if (!parts || player_of_body(parts - BODY_PARTS_OFFSET) != player) continue;
        const uint32_t *torso = MODEL_TABLE[player_torso_model[player]].object;
        if (!torso) return NULL;
        if (torso != equipment_source[player]) {
            free(equipment_objects[player]);
            equipment_objects[player] = build_equipment_object(torso);
            equipment_source[player] = torso;
            log_line("characters: player %d keeps the keyboard and Dreamcast (%s)", player + 1,
                     equipment_objects[player] ? "drawn with the custom torso" : "not found in the player torso");
        }
        return equipment_objects[player];
    }
    return NULL;
}

static int torso_model_of_kind(const uint8_t *const *bodies, int kind) {
    const uint8_t *root = bodies[kind];
    if (!root || IsBadReadPtr(root, BODY_NODE_CHILDREN)) return 0;
    short children = *(const short *)(root + BODY_NODE_CHILD_COUNT);
    for (int i = 0; i < children; i++) {
        const uint8_t *child = *(const uint8_t *const *)(root + BODY_NODE_CHILDREN + 4 * i);
        if (child && *(const short *)(child + 0x14) == TORSO_PART_ID) return (int)*(const uint32_t *)child;
    }
    return 0;
}

int character_override_active(void) {
    return player_kind[0] != NORMAL || player_kind[1] != NORMAL;
}

void character_load_extra_banks(int bank, void(__cdecl *request_bank_load)(int bank)) {
    int player = bank - FIRST_PLAYER_BANK;
    if (player < 0 || player >= PLAYERS || player_kind[player] == NORMAL) return;
    for (int i = 0; i < player_bank_count[player]; i++) request_bank_load(player_banks[player][i]);
}

int character_keeps_bank(int bank) {
    return is_override_bank(bank);
}

static void add_body_banks(int player, const uint8_t *node, const unsigned short *bank_of_model, int depth) {
    if (!node || depth > MAX_BODY_DEPTH || IsBadReadPtr(node, BODY_NODE_CHILDREN)) return;
    uint32_t model_id = *(const uint32_t *)node;
    short children = *(const short *)(node + BODY_NODE_CHILD_COUNT);
    if (model_id && model_id < MODEL_ID_LIMIT) {
        int bank = bank_of_model[model_id];
        int known = bank == NO_BANK;
        for (int j = 0; j < player_bank_count[player]; j++) known |= player_banks[player][j] == bank;
        if (!known && player_bank_count[player] < MAX_KIND_BANKS) player_banks[player][player_bank_count[player]++] = bank;
    }
    for (int i = 0; i < children; i++) {
        add_body_banks(player, *(const uint8_t *const *)(node + BODY_NODE_CHILDREN + 4 * i), bank_of_model, depth + 1);
    }
}

static int read_kind_banks(int player, HMODULE table) {
    const uint8_t *const *bodies = (const uint8_t *const *)GetProcAddress(table, "BodyInitTbl");
    const unsigned short *bank_of_model = (const unsigned short *)GetProcAddress(table, "PbkTbl");
    if (!bodies || !bank_of_model || player_kind[player] >= MAX_KINDS) return 0;
    for (int kind = 0; kind <= player_kind[player]; kind++) {
        if (IsBadReadPtr(&bodies[kind], 4) || !bodies[kind]) return 0;
    }
    add_body_banks(player, bodies[player_kind[player]], bank_of_model, 0);
    return player_bank_count[player] > 0;
}

static MH_STATUS hook_address(DWORD address, void *hook, void **original) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, original);
    return status == MH_OK ? MH_EnableHook((void *)address) : status;
}

static short original_player_kind[PLAYERS];
static int kinds_table_shows_choice;

static void write_player_kind_slot(int player, short kind) {
    volatile short *slot = &GAMEPLAY_PLAYER_KINDS[player * GAMEPLAY_PLAYER_KIND_STRIDE];
    DWORD old_protection;
    if (VirtualProtect((void *)slot, sizeof *slot, PAGE_READWRITE, &old_protection)) {
        *slot = kind;
        VirtualProtect((void *)slot, sizeof *slot, old_protection, &old_protection);
    }
}

static void keep_staff_credits_players_normal(void) {
    int show_choice = GAME_TYPE != STAFF_CREDITS_GAME_TYPE;
    if (show_choice == kinds_table_shows_choice) return;
    kinds_table_shows_choice = show_choice;
    for (int player = 0; player < PLAYERS; player++) {
        if (player_kind[player] != NORMAL)
            write_player_kind_slot(player, show_choice ? (short)player_kind[player] : original_player_kind[player]);
    }
    log_line("characters: %s", show_choice ? "chosen player characters back" : "Staff Credits: normal players");
}

void install_character_hooks(const char *ini_path) {

    player_kind[0] = GetPrivateProfileIntA("cheats", "player1_character", NORMAL, ini_path);
    player_kind[1] = GetPrivateProfileIntA("cheats", "player2_character", NORMAL, ini_path);
    HMODULE table = GetModuleHandleA("Table.dll");
    for (int player = 0; player < PLAYERS; player++) {
        original_player_kind[player] = GAMEPLAY_PLAYER_KINDS[player * GAMEPLAY_PLAYER_KIND_STRIDE];
        if (player_kind[player] < 0) player_kind[player] = NORMAL;
        if (player_kind[player] == NORMAL) continue;
        if (!table || !read_kind_banks(player, table)) {
            log_line("characters: character kind %d for player %d not found", player_kind[player], player + 1);
            player_kind[player] = NORMAL;
            continue;
        }
        write_player_kind_slot(player, (short)player_kind[player]);
        kinds_table_shows_choice = 1;
        log_line("characters: player %d plays as character kind %d (%d model banks, first %d)", player + 1,
                 player_kind[player], player_bank_count[player], player_bank_count[player] ? player_banks[player][0] : -1);
    }
    if (!character_override_active()) return;
    frame_hooks_on_frame_start(keep_staff_credits_players_normal);
    carry_equipment = GetPrivateProfileIntA("cheats", "keep_equipment", 1, ini_path);
    const uint8_t *const *bodies = (const uint8_t *const *)GetProcAddress(table, "BodyInitTbl");
    for (int player = 0; carry_equipment && bodies && player < PLAYERS; player++) {
        if (player_kind[player] == NORMAL || (player_kind[player] >= PLAYER_KIND_FIRST && player_kind[player] <= 61)) continue;
        custom_torso_model[player] = torso_model_of_kind(bodies, player_kind[player]);
        player_torso_model[player] = torso_model_of_kind(bodies, PLAYER_KIND_FIRST + player);
    }
    if (carry_equipment) overlay_set_object_drawn_callback(equipment_for_drawn_object);
    log_line("characters: hooks prepare_event_character %s, init_scripted_character %s",
             MH_StatusToString(hook_address(PREPARE_EVENT_CHARACTER_VA, prepare_event_character_hook,
                                            (void **)&original_prepare_event_character)),
             MH_StatusToString(hook_address(INIT_SCRIPTED_CHARACTER_VA, init_scripted_character_hook,
                                            (void **)&original_init_scripted_character)));
}
