#include <windows.h>
#include <stdint.h>
#include "mutators.h"
#include "frame_hooks.h"
#include "word_mutators.h"
#include "log.h"
#include "MinHook.h"

#define PLAY_SOUND_VA 0x0049c380
#define DAMAGE_PLAYER_VA 0x004872b0
#define MISS_SOUND_PLAYER1 0x005517a9
#define MISS_SOUND_PLAYER2 0x005617a9
#define GAME_MODE (*(volatile uint32_t *)0x00a3e308)
#define ARCADE_MODE 6
#define PLAYER_LIFE(player) (*(volatile int16_t *)(0x00a18626 + (player) * 0x210))
#define PLAYER_GRACE_FRAMES(player) (((volatile uint32_t *)0x00a1b730)[player])
#define WORD_LEVEL (*(volatile int16_t *)0x00a3e9a2)
#define HARDEST_WORD_LEVEL 15
#define PLAYERS 2

enum { LEVEL_NORMAL, LEVEL_EASIEST, LEVEL_HARDEST };

typedef void(__cdecl *play_sound_fn)(uint32_t sound);
typedef void(__cdecl *damage_player_fn)(int player, int knock_back, int16_t direction);

static play_sound_fn original_play_sound;
static damage_player_fn damage_player = (damage_player_fn)DAMAGE_PLAYER_VA;
static damage_player_fn original_damage_player;
static int infinite_lives;
static int hits_absorbed;
static int max_lives;
static int typos_hurt;
static int word_level = LEVEL_NORMAL;
static int pending_typo[PLAYERS];
static int typo_hits;

static void __cdecl play_sound_hook(uint32_t sound) {
    if (sound == MISS_SOUND_PLAYER1) pending_typo[0] = 1;
    else if (sound == MISS_SOUND_PLAYER2) pending_typo[1] = 1;
    original_play_sound(sound);
}

static void __cdecl damage_player_hook(int player, int knock_back, int16_t direction) {
    if (player < 0 || player >= PLAYERS) {
        original_damage_player(player, knock_back, direction);
        return;
    }
    int16_t life = PLAYER_LIFE(player);
    if (life > 0 && life < 2) PLAYER_LIFE(player) = 2;
    original_damage_player(player, knock_back, direction);
    if (life > 0) PLAYER_LIFE(player) = life;
    if (++hits_absorbed <= 5) log_line("cheats: player %d hit, life kept at %d", player + 1, life);
}

static void apply_mutators(void) {
    if (word_level == LEVEL_EASIEST) WORD_LEVEL = 0;
    else if (word_level == LEVEL_HARDEST) WORD_LEVEL = HARDEST_WORD_LEVEL;
    if (GAME_MODE != ARCADE_MODE) {
        pending_typo[0] = pending_typo[1] = 0;
        return;
    }
    for (int player = 0; player < PLAYERS; player++) {
        if (max_lives && PLAYER_LIFE(player) > max_lives) PLAYER_LIFE(player) = (int16_t)max_lives;
        if (!pending_typo[player]) continue;
        pending_typo[player] = 0;
        if (PLAYER_LIFE(player) <= 0 || PLAYER_GRACE_FRAMES(player)) continue;
        damage_player(player, 0, 0);
        if (++typo_hits <= 10) log_line("mutators: typo cost player %d a life (%d left)", player + 1, PLAYER_LIFE(player));
    }
}

void install_mutators(const char *ini_path) {
    char text[32];
    max_lives = GetPrivateProfileIntA("mutators", "max_lives", 0, ini_path);
    if (max_lives < 0) max_lives = 0;
    typos_hurt = GetPrivateProfileIntA("mutators", "typos_hurt", 0, ini_path);
    GetPrivateProfileStringA("mutators", "word_level", "normal", text, sizeof text, ini_path);
    word_level = !lstrcmpiA(text, "easiest") ? LEVEL_EASIEST : !lstrcmpiA(text, "hardest") ? LEVEL_HARDEST : LEVEL_NORMAL;
    infinite_lives = GetPrivateProfileIntA("cheats", "infinite_lives", 0, ini_path);
    if (infinite_lives) {
        MH_STATUS status = MH_CreateHook((void *)DAMAGE_PLAYER_VA, damage_player_hook, (void **)&original_damage_player);
        if (status == MH_OK) status = MH_EnableHook((void *)DAMAGE_PLAYER_VA);
        log_line("cheats: infinite lives (hook damage_player: %s)", MH_StatusToString(status));
    }
    if (!max_lives && !typos_hurt && word_level == LEVEL_NORMAL) return;
    frame_hooks_on_frame_end(apply_mutators);
    const char *sound_status = "not needed";
    if (typos_hurt) {
        MH_STATUS status = MH_CreateHook((void *)PLAY_SOUND_VA, play_sound_hook, (void **)&original_play_sound);
        if (status == MH_OK) status = MH_EnableHook((void *)PLAY_SOUND_VA);
        sound_status = MH_StatusToString(status);
    }
    log_line("mutators: max lives %d, typos hurt %d (hook play_sound: %s), word level %s", max_lives, typos_hurt,
             sound_status, text);
}
