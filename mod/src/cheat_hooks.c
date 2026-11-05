#include <windows.h>
#include "cheat_hooks.h"
#include "character_hooks.h"
#include "randomizer.h"
#include "log.h"
#include "MinHook.h"

#define UPDATE_PADS_VA 0x00456650
#define REQUEST_BANK_LOAD_VA 0x0049ccc0
#define REQUEST_BANK_FREE_VA 0x0049ce30
#define ITEM_EFFECTS (*(volatile unsigned *)0x009f0be0)
#define BALLOON_HEAD_EFFECT 0x02
#define ZOMBIE_SUIT_EFFECT 0x40
#define ELDERLY_SUIT_EFFECT 0x80
#define PLAYER1_BANK 0xc4
#define PLAYER2_BANK 0xc5

typedef void(__cdecl *update_pads_fn)(void);
typedef void(__cdecl *bank_request_fn)(int bank);

static update_pads_fn original_update_pads;
static bank_request_fn original_request_bank_load;
static bank_request_fn original_request_bank_free;
static unsigned forced_effects;
static unsigned cleared_effects;
static int keep_player_banks;
static int log_banks;

static void __cdecl request_bank_load_hook(int bank) {
    if (log_banks) log_line("banks: load request %d (item effects %X)", bank, ITEM_EFFECTS);
    if (keep_player_banks && (bank == PLAYER1_BANK || bank == PLAYER2_BANK)) {
        unsigned effects = ITEM_EFFECTS;
        ITEM_EFFECTS = effects & ~(ZOMBIE_SUIT_EFFECT | ELDERLY_SUIT_EFFECT);
        original_request_bank_load(bank);
        ITEM_EFFECTS = effects;
    }
    original_request_bank_load(bank);
    character_load_extra_banks(bank, original_request_bank_load);
    randomizer_load_extra_banks(bank, original_request_bank_load);
}

static void __cdecl request_bank_free_hook(int bank) {
    int kept = (keep_player_banks && (bank == PLAYER1_BANK || bank == PLAYER2_BANK)) || character_keeps_bank(bank) ||
               randomizer_keeps_bank(bank);
    if (log_banks) log_line("banks: free request %d (item effects %X)%s", bank, ITEM_EFFECTS, kept ? ", kept" : "");
    if (!kept) original_request_bank_free(bank);
}

static void __cdecl update_pads_hook(void) {
    original_update_pads();
    ITEM_EFFECTS = (ITEM_EFFECTS & ~cleared_effects) | forced_effects;
}

static MH_STATUS hook_address(DWORD address, void *hook, void **original) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, original);
    return status == MH_OK ? MH_EnableHook((void *)address) : status;
}

void install_cheat_hooks(const char *ini_path) {
    char suit[16] = "";
    GetPrivateProfileStringA("cheats", "suit", "none", suit, sizeof suit, ini_path);
    if (GetPrivateProfileIntA("cheats", "big_heads", 0, ini_path)) forced_effects |= BALLOON_HEAD_EFFECT;
    if (!lstrcmpiA(suit, "zombie")) {
        forced_effects |= ZOMBIE_SUIT_EFFECT;
        cleared_effects |= ELDERLY_SUIT_EFFECT;
    } else if (!lstrcmpiA(suit, "elderly")) {
        forced_effects |= ELDERLY_SUIT_EFFECT;
        cleared_effects |= ZOMBIE_SUIT_EFFECT;
    }
    log_banks = GetPrivateProfileIntA("debug", "log_banks", 0, ini_path);
    keep_player_banks = (forced_effects & (ZOMBIE_SUIT_EFFECT | ELDERLY_SUIT_EFFECT)) != 0;
    if (keep_player_banks || log_banks || character_override_active() || randomizer_active()) {
        log_line("banks: hooks request_bank_load %s, request_bank_free %s%s",
                 MH_StatusToString(hook_address(REQUEST_BANK_LOAD_VA, request_bank_load_hook,
                                                (void **)&original_request_bank_load)),
                 MH_StatusToString(hook_address(REQUEST_BANK_FREE_VA, request_bank_free_hook,
                                                (void **)&original_request_bank_free)),
                 keep_player_banks ? "; player banks stay loaded while suited, so zombies/people sharing them stay visible"
                                   : "");
    }
    if (!forced_effects) return;
    log_line("cheats:%s%s (hook update_pads: %s)", forced_effects & BALLOON_HEAD_EFFECT ? " big heads" : "",
             forced_effects & ZOMBIE_SUIT_EFFECT ? " zombie suit"
             : forced_effects & ELDERLY_SUIT_EFFECT ? " elderly suit" : "",
             MH_StatusToString(hook_address(UPDATE_PADS_VA, update_pads_hook, (void **)&original_update_pads)));
}
