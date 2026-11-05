#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "randomizer.h"
#include "online_hooks.h"
#include "replay.h"
#include "enemy_counts.h"
#include "log.h"
#include "MinHook.h"

#define ONLINE_DEFAULT_SEED 20261028u
#define MAX_KINDS 128
#define HUMANOID_NODES 16
#define BODY_NODE_CHILD_COUNT 0x16
#define BODY_NODE_CHILDREN 0x18
#define MAX_BODY_DEPTH 32
#define MODEL_ID_LIMIT 0x24b3
#define NO_BANK 0x102
#define MAX_KIND_BANKS 8
#define MAX_BANKS 512
#define SUBTITLE_EVENT_TABLE ((short *)0x0051e8f0)
#define SUBTITLE_EVENT_TABLE_END ((short *)0x0051eea8)
#define SUBTITLE_SOUND(id) (*(const uint32_t *)(0x0051cfdc + (id) * 16))
#define MAX_SUBTITLE_ID 512
#define VOICE_SOUND_KIND 2
#define ITEM_EFFECT_VA 0x004570c0
#define FIRST_RANDOM_ITEM 5
#define LAST_RANDOM_ITEM 15
#define REGISTER_OBJECT_VA 0x00421f80
#define OBJECT_HELD_MODEL 0x13f4
#define OBJECT_KIND 0x1fc
#define OBJECT_FLIGHT_A 0x133c
#define OBJECT_FLIGHT_B 0x1340
#define THROWN_WEAPON_KIND 5
#define THROWN_WEAPON_FLIGHT 7
#define GAME_FRAME (*(volatile uint32_t *)0x00a18fac)
#define TRACKED_WEAPONS 32
#define MODEL_LOADED(id) (*(volatile short *)(0x009f3acc + (id) * 16) < 0)

static const int zombie_kinds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22, 24, 25};
static const int people_kinds[] = {32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
                                   51, 52, 53, 54, 55, 56, 62, 63, 64, 66, 67};

static int active;
static uint32_t random_state;
static const unsigned short *bank_of_model;
static int kind_banks[MAX_KINDS][MAX_KIND_BANKS];
static int kind_bank_count[MAX_KINDS];
static int shown_kind[MAX_KINDS];
static unsigned char kept_bank[MAX_BANKS];
static int kind_total;

static void *original_item_effect __asm__("todmod_original_item_effect");
static int item_changes;

static const int weapon_models[] = {631, 632, 633, 634, 635, 636, 637, 638, 8072};
static int weapons_active;
static int weapon_changes;
static struct {
    char *object;
    uint32_t last_frame;
    int model;
} tracked_weapons[TRACKED_WEAPONS];
typedef void(__cdecl *register_object_fn)(char *object);
static register_object_fn original_register_object;

uint32_t randomizer_next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int randomizer_active(void) {
    return active;
}

static int count_nodes(const uint8_t *node, int depth) {
    if (!node || depth > MAX_BODY_DEPTH || IsBadReadPtr(node, BODY_NODE_CHILDREN)) return 0;
    int total = 1;
    short children = *(const short *)(node + BODY_NODE_CHILD_COUNT);
    for (int i = 0; i < children; i++) total += count_nodes(*(const uint8_t *const *)(node + BODY_NODE_CHILDREN + 4 * i), depth + 1);
    return total;
}

static void add_body_banks(int kind, const uint8_t *node, int depth) {
    if (!node || depth > MAX_BODY_DEPTH || IsBadReadPtr(node, BODY_NODE_CHILDREN)) return;
    uint32_t model_id = *(const uint32_t *)node;
    if (model_id && model_id < MODEL_ID_LIMIT && bank_of_model[model_id] != NO_BANK) {
        int bank = bank_of_model[model_id], known = 0;
        for (int i = 0; i < kind_bank_count[kind]; i++) known |= kind_banks[kind][i] == bank;
        if (!known && kind_bank_count[kind] < MAX_KIND_BANKS) kind_banks[kind][kind_bank_count[kind]++] = bank;
    }
    short children = *(const short *)(node + BODY_NODE_CHILD_COUNT);
    for (int i = 0; i < children; i++) add_body_banks(kind, *(const uint8_t *const *)(node + BODY_NODE_CHILDREN + 4 * i), depth + 1);
}

static const int own_look_kinds[] = {1, 2, 9, 18, 19, 20, 22, 24, 25};

static int chosen_player_kinds[2] = {-1, -1};

static int needs_own_look(int kind) {
    if (kind == chosen_player_kinds[0] || kind == chosen_player_kinds[1]) return 1;
    for (size_t i = 0; i < sizeof own_look_kinds / sizeof *own_look_kinds; i++) {
        if (own_look_kinds[i] == kind) return 1;
    }
    for (size_t w = 0; w < sizeof weapon_models / sizeof *weapon_models; w++) {
        for (int i = 0; i < kind_bank_count[kind]; i++) {
            if (kind_banks[kind][i] == bank_of_model[weapon_models[w]]) return 1;
        }
    }
    return 0;
}

static int usable_pool(const uint8_t *const *bodies, const int *kinds, int count, int *out) {
    int usable = 0;
    for (int i = 0; i < count; i++) {
        if (kinds[i] >= kind_total || count_nodes(bodies[kinds[i]], 0) != HUMANOID_NODES) continue;
        if (needs_own_look(kinds[i])) {
            log_line("randomizer: kind %d keeps its look (%s)", kinds[i],
                     kinds[i] == chosen_player_kinds[0] || kinds[i] == chosen_player_kinds[1] ? "chosen as a player character"
                                                                                               : "its moves need its own body");
            continue;
        }
        out[usable++] = kinds[i];
    }
    return usable;
}

static void shuffle_pool(const int *pool, int count) {
    int order[MAX_KINDS];
    memcpy(order, pool, (size_t)count * sizeof *order);
    for (int i = count - 1; i > 0; i--) {
        int j = (int)(randomizer_next() % (uint32_t)(i + 1));
        int swap = order[i];
        order[i] = order[j];
        order[j] = swap;
    }
    for (int i = 0; i < count; i++) shown_kind[pool[i]] = order[i];
}

static void show_zombies_as_people(const int *zombies, int zombie_count, const int *people, int people_count,
                                   int percent) {
    if (percent <= 0 || !people_count) return;
    int changed = 0;
    for (int i = 0; i < zombie_count; i++) {
        if ((int)(randomizer_next() % 100) >= percent) continue;
        shown_kind[zombies[i]] = people[randomizer_next() % (uint32_t)people_count];
        changed++;
    }
    log_line("randomizer: %d of %d zombie kinds look like people", changed, zombie_count);
}

static void permute_table(void **table, const char *name) {
    if (!table) return;
    void *original[MAX_KINDS];
    size_t bytes = (size_t)kind_total * sizeof *original;
    memcpy(original, table, bytes);
    DWORD protection;
    if (!VirtualProtect(table, bytes, PAGE_READWRITE, &protection)) {
        log_line("randomizer: cannot write %s", name);
        return;
    }
    for (int kind = 0; kind < kind_total; kind++) table[kind] = original[shown_kind[kind]];
    VirtualProtect(table, bytes, protection, &protection);
}

static void load_weapon_banks(int bank, void(__cdecl *request_bank_load)(int bank)) {
    int brings = 0;
    for (size_t i = 0; i < sizeof weapon_models / sizeof *weapon_models; i++) brings |= bank_of_model[weapon_models[i]] == bank;
    if (!brings) return;
    for (size_t i = 0; i < sizeof weapon_models / sizeof *weapon_models; i++) {
        int extra = bank_of_model[weapon_models[i]];
        if (extra == bank || extra >= MAX_BANKS || kept_bank[extra]) continue;
        log_line("randomizer: weapon bank %d brings weapon bank %d", bank, extra);
        kept_bank[extra] = 1;
        request_bank_load(extra);
    }
}

void randomizer_load_extra_banks(int bank, void(__cdecl *request_bank_load)(int bank)) {
    if (!active) return;
    if (weapons_active) load_weapon_banks(bank, request_bank_load);
    for (int kind = 0; kind < MAX_KINDS; kind++) {
        if (shown_kind[kind] == kind) continue;
        int uses_bank = 0;
        for (int i = 0; i < kind_bank_count[kind]; i++) uses_bank |= kind_banks[kind][i] == bank;
        if (!uses_bank) continue;
        int shown = shown_kind[kind];
        for (int i = 0; i < kind_bank_count[shown]; i++) {
            int extra = kind_banks[shown][i];
            if (extra == bank || extra >= MAX_BANKS) continue;
            if (!kept_bank[extra]) log_line("randomizer: bank %d brings bank %d (kind %d looks like kind %d)", bank, extra, kind, shown);
            kept_bank[extra] = 1;
            request_bank_load(extra);
        }
    }
}

int randomizer_keeps_bank(int bank) {
    return active && bank >= 0 && bank < MAX_BANKS && kept_bank[bank];
}

static void shuffle_voice_lines(void) {
    int voiced[MAX_SUBTITLE_ID], count = 0;
    unsigned char seen[MAX_SUBTITLE_ID] = {0};
    for (short *entry = SUBTITLE_EVENT_TABLE; entry < SUBTITLE_EVENT_TABLE_END; entry++) {
        int id = *entry;
        if (id <= 0 || id >= MAX_SUBTITLE_ID || seen[id] || SUBTITLE_SOUND(id) >> 28 != VOICE_SOUND_KIND) continue;
        seen[id] = 1;
        voiced[count++] = id;
    }
    int replacement[MAX_SUBTITLE_ID];
    for (int i = 0; i < MAX_SUBTITLE_ID; i++) replacement[i] = i;
    int order[MAX_SUBTITLE_ID];
    memcpy(order, voiced, (size_t)count * sizeof *order);
    for (int i = count - 1; i > 0; i--) {
        int j = (int)(randomizer_next() % (uint32_t)(i + 1));
        int swap = order[i];
        order[i] = order[j];
        order[j] = swap;
    }
    for (int i = 0; i < count; i++) replacement[voiced[i]] = order[i];
    DWORD bytes = (DWORD)((char *)SUBTITLE_EVENT_TABLE_END - (char *)SUBTITLE_EVENT_TABLE), protection;
    if (!VirtualProtect(SUBTITLE_EVENT_TABLE, bytes, PAGE_READWRITE, &protection)) return;
    for (short *entry = SUBTITLE_EVENT_TABLE; entry < SUBTITLE_EVENT_TABLE_END; entry++) {
        if (*entry > 0 && *entry < MAX_SUBTITLE_ID) *entry = (short)replacement[*entry];
    }
    VirtualProtect(SUBTITLE_EVENT_TABLE, bytes, protection, &protection);
    log_line("randomizer: %d voice lines shuffled (subtitles follow their voices)", count);
}

static int choose_weapon(int original) {
    int loaded[sizeof weapon_models / sizeof *weapon_models], count = 0;
    for (size_t i = 0; i < sizeof weapon_models / sizeof *weapon_models; i++) {
        if (MODEL_LOADED(weapon_models[i])) loaded[count++] = weapon_models[i];
    }
    if (!count) return original;
    int chosen = loaded[randomizer_next() % (uint32_t)count];
    if (weapon_changes++ < 32) log_line("randomizer: thrown weapon %d -> %d (%d loaded)", original, chosen, count);
    return chosen;
}

static int weapon_for_thrown_object(char *object) {
    uint32_t now = GAME_FRAME;
    int free_slot = 0;
    for (int i = 0; i < TRACKED_WEAPONS; i++) {
        if (tracked_weapons[i].object == object && now - tracked_weapons[i].last_frame <= 1) {
            tracked_weapons[i].last_frame = now;
            return tracked_weapons[i].model;
        }
        if (now - tracked_weapons[i].last_frame < now - tracked_weapons[free_slot].last_frame) continue;
        free_slot = i;
    }
    tracked_weapons[free_slot].object = object;
    tracked_weapons[free_slot].last_frame = now;
    tracked_weapons[free_slot].model = choose_weapon(*(int *)(object + OBJECT_HELD_MODEL));
    return tracked_weapons[free_slot].model;
}

static void __cdecl register_object_hook(char *object) {
    if (*(int *)(object + OBJECT_KIND) == THROWN_WEAPON_KIND && *(int *)(object + OBJECT_FLIGHT_A) == THROWN_WEAPON_FLIGHT &&
        *(int *)(object + OBJECT_FLIGHT_B) == THROWN_WEAPON_FLIGHT) {
        *(int *)(object + OBJECT_HELD_MODEL) = weapon_for_thrown_object(object);
    }
    original_register_object(object);
}

static int __cdecl random_item(int item) __asm__("todmod_random_item");
__attribute__((used)) static int __cdecl random_item(int item) {
    if (item < FIRST_RANDOM_ITEM || item > LAST_RANDOM_ITEM) return item;
    int chosen = FIRST_RANDOM_ITEM + (int)(randomizer_next() % (LAST_RANDOM_ITEM - FIRST_RANDOM_ITEM + 1));
    if (item_changes++ < 32) log_line("randomizer: item %d -> %d", item, chosen);
    return chosen;
}

__attribute__((naked)) static void item_effect_hook(void) {
    __asm__ volatile(
        "pushal\n"
        "pushl %edi\n"
        "call todmod_random_item\n"
        "addl $4, %esp\n"
        "movl %eax, (%esp)\n"
        "popal\n"
        "jmp *todmod_original_item_effect\n");
}

void install_randomizer(const char *ini_path) {
    for (int kind = 0; kind < MAX_KINDS; kind++) shown_kind[kind] = kind;
    if (!GetPrivateProfileIntA("randomizer", "enabled", 0, ini_path)) return;
    HMODULE table = GetModuleHandleA("Table.dll");
    void **bodies = table ? (void **)GetProcAddress(table, "BodyInitTbl") : NULL;
    bank_of_model = table ? (const unsigned short *)GetProcAddress(table, "PbkTbl") : NULL;
    if (!bodies || !bank_of_model) {
        log_line("randomizer: Table.dll character tables not found");
        return;
    }
    uint32_t seed = (uint32_t)GetPrivateProfileIntA("randomizer", "seed", 0, ini_path);
    if (!seed && online_role_from_settings(ini_path)) {
        seed = ONLINE_DEFAULT_SEED;
        log_line("randomizer: online play needs the same seed on both copies; seed 0 becomes %u", seed);
    }
    if (!seed) seed = GetTickCount() ^ (GetCurrentProcessId() << 16);
    if (!seed) seed = 1;
    if (replay_forced_randomizer_seed(&seed)) log_line("randomizer: seed %u from the replay", seed);
    replay_note_randomizer_seed(seed);
    random_state = seed;
    for (kind_total = 0; kind_total < MAX_KINDS && !IsBadReadPtr(&bodies[kind_total], 4) && bodies[kind_total]; kind_total++) {
        add_body_banks(kind_total, bodies[kind_total], 0);
    }
    chosen_player_kinds[0] = GetPrivateProfileIntA("cheats", "player1_character", -1, ini_path);
    chosen_player_kinds[1] = GetPrivateProfileIntA("cheats", "player2_character", -1, ini_path);
    int zombies[MAX_KINDS], people[MAX_KINDS * 2];
    int zombie_count = usable_pool((const uint8_t *const *)bodies, zombie_kinds, sizeof zombie_kinds / sizeof *zombie_kinds, zombies);
    int people_count = usable_pool((const uint8_t *const *)bodies, people_kinds, sizeof people_kinds / sizeof *people_kinds, people);
    int enemies = GetPrivateProfileIntA("randomizer", "enemies", 1, ini_path);
    int persons = GetPrivateProfileIntA("randomizer", "people", 1, ini_path);
    if (GetPrivateProfileIntA("randomizer", "mix", 0, ini_path) && enemies && persons) {
        memcpy(people + people_count, zombies, (size_t)zombie_count * sizeof *zombies);
        shuffle_pool(people, people_count + zombie_count);
    } else {
        if (enemies) shuffle_pool(zombies, zombie_count);
        if (persons) shuffle_pool(people, people_count);
    }
    show_zombies_as_people(zombies, zombie_count, people, people_count,
                           GetPrivateProfileIntA("randomizer", "human_zombies", 0, ini_path));
    permute_table(bodies, "BodyInitTbl");
    permute_table((void **)GetProcAddress(table, "BeltVtxDb"), "BeltVtxDb");
    permute_table((void **)GetProcAddress(table, "ShadowTbl"), "ShadowTbl");
    active = 1;
    if (GetPrivateProfileIntA("randomizer", "voices", 1, ini_path)) shuffle_voice_lines();
    if (GetPrivateProfileIntA("randomizer", "items", 1, ini_path)) {
        MH_STATUS status = MH_CreateHook((void *)ITEM_EFFECT_VA, item_effect_hook, &original_item_effect);
        if (status == MH_OK) status = MH_EnableHook((void *)ITEM_EFFECT_VA);
        log_line("randomizer: items %d-%d random when picked up (hook item_effect: %s)", FIRST_RANDOM_ITEM,
                 LAST_RANDOM_ITEM, MH_StatusToString(status));
    }
    if (GetPrivateProfileIntA("randomizer", "weapons", 1, ini_path)) {
        weapons_active = 1;
        MH_STATUS status = MH_CreateHook((void *)REGISTER_OBJECT_VA, register_object_hook, (void **)&original_register_object);
        if (status == MH_OK) status = MH_EnableHook((void *)REGISTER_OBJECT_VA);
        log_line("randomizer: thrown weapons random (hook register_object: %s)", MH_StatusToString(status));
    }
    install_enemy_counts(ini_path, seed);
    char swaps[1024] = "";
    size_t used = 0;
    for (int kind = 0; kind < MAX_KINDS && used + 16 < sizeof swaps; kind++) {
        if (shown_kind[kind] != kind) used += (size_t)snprintf(swaps + used, sizeof swaps - used, " %d->%d", kind, shown_kind[kind]);
    }
    log_line("randomizer: seed %u, character looks%s", seed, swaps);
}
