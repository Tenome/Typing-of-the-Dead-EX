#include <windows.h>
#include "input_hooks.h"
#include "log.h"
#include "MinHook.h"
#include "word_pack.h"
#include "dictionary_hooks.h"
#include "online_hooks.h"
#include "replay.h"

#define KEY_TO_CHARACTER_VA 0x00456700
#define BUILD_KEY_USAGE_TABLE_VA 0x0049b290
#define SCANCODE_TO_USAGE ((unsigned char *)0x0077a628)
#define JUDGE_KEY_VA 0x004026f0
#define TYPING_STATE_CURRENT_UNIT 0x21
#define TYPING_STATE_READING 1
#define WORD_ROMAJI_TEXT 8
#define WORD_ROMAJI_POSITION 0x49
#define KEY_DOWN_BIT 0x8000
#define SHIFTED_E_CODE 0x12
#define SHIFTED_Z_CODE 0x13
#define SHIFT_MODIFIERS 0x22
#define TO_UNICODE_KEEP_KEYBOARD_STATE 0x4
#define PRESSED 0x80
#define FIRST_PRINTABLE 0x20
#define LAST_PRINTABLE 0x7E
#define NO_CHARACTER 0xFF
#define WORD_BOX_TASK_VA 0x004ca960
#define NEWEST_KEY(player) ((volatile unsigned char *)(0x007579c5 + (player) * 0x2c))
#define PLAYER_KEY_SLOTS 2
#define USAGE_ESCAPE 0x29
#define USAGE_BACKSPACE 0x2A

typedef void(__cdecl *word_box_task_fn)(void *task);
static word_box_task_fn original_word_box_task;

typedef void(__cdecl *build_key_usage_table_fn)(void);
typedef unsigned char(__cdecl *key_to_character_fn)(unsigned char key, unsigned char modifiers);

static key_to_character_fn original_key_to_character;
static build_key_usage_table_fn original_build_key_usage_table;
typedef unsigned char(__cdecl *judge_key_fn)(int *typing_state, char character);
static judge_key_fn original_judge_key;
static char last_key_letter;
static int last_key_case = WORD_CASE_ANY;
static int log_typing;
static int check_letter_case;
static int autotype;

typedef struct {
    unsigned char usage;
    unsigned char scancode;
} usage_scancode;

static const usage_scancode usage_scancodes[] = {
    {0x04, 0x1E}, {0x05, 0x30}, {0x06, 0x2E}, {0x07, 0x20}, {0x08, 0x12}, {0x09, 0x21}, {0x0A, 0x22},
    {0x0B, 0x23}, {0x0C, 0x17}, {0x0D, 0x24}, {0x0E, 0x25}, {0x0F, 0x26}, {0x10, 0x32}, {0x11, 0x31},
    {0x12, 0x18}, {0x13, 0x19}, {0x14, 0x10}, {0x15, 0x13}, {0x16, 0x1F}, {0x17, 0x14}, {0x18, 0x16},
    {0x19, 0x2F}, {0x1A, 0x11}, {0x1B, 0x2D}, {0x1C, 0x15}, {0x1D, 0x2C}, {0x1E, 0x02}, {0x1F, 0x03},
    {0x20, 0x04}, {0x21, 0x05}, {0x22, 0x06}, {0x23, 0x07}, {0x24, 0x08}, {0x25, 0x09}, {0x26, 0x0A},
    {0x27, 0x0B}, {0x2C, 0x39}, {0x2D, 0x0C}, {0x2E, 0x0D}, {0x2F, 0x1A}, {0x30, 0x1B}, {0x31, 0x2B},
    {0x32, 0x2B}, {0x33, 0x27}, {0x34, 0x28}, {0x35, 0x29}, {0x36, 0x33}, {0x37, 0x34}, {0x38, 0x35},
    {0x64, 0x56}, {0x87, 0x73}, {0x89, 0x7D},
};

enum { LAYOUT_JAPANESE, LAYOUT_WINDOWS, LAYOUT_TABLE };

typedef struct {
    const char *name;
    const char *plain[4];
    const char *shifted[4];
} keyboard_layout;

static const unsigned char layout_row_first_scancode[4] = {0x02, 0x10, 0x1E, 0x2C};

static const keyboard_layout keyboard_layouts[] = {
    {"qwerty",
     {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./"},
     {"!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>?"}},
    {"dvorak",
     {"1234567890[]", "',.pyfgcrl/=", "aoeuidhtns-", ";qjkxbmwvz"},
     {"!@#$%^&*(){}", "\"<>PYFGCRL?+", "AOEUIDHTNS_", ":QJKXBMWVZ"}},
    {"colemak",
     {"1234567890-=", "qwfpgjluy;[]", "arstdhneio'", "zxcvbkm,./"},
     {"!@#$%^&*()_+", "QWFPGJLUY:{}", "ARSTDHNEIO\"", "ZXCVBKM<>?"}},
    {"workman",
     {"1234567890-=", "qdrwbjfup;[]", "ashtgyneoi'", "zxmcvkl,./"},
     {"!@#$%^&*()_+", "QDRWBJFUP:{}", "ASHTGYNEOI\"", "ZXMCVKL<>?"}},
};

static int layout_mode;
static const keyboard_layout *chosen_layout;

static const keyboard_layout *find_keyboard_layout(const char *name) {
    for (int i = 0; i < (int)(sizeof keyboard_layouts / sizeof keyboard_layouts[0]); i++) {
        if (lstrcmpiA(keyboard_layouts[i].name, name) == 0) return &keyboard_layouts[i];
    }
    return NULL;
}

static int ascii_from_layout_table(const keyboard_layout *layout, unsigned scancode, int shifted) {
    if (scancode == 0x29) return shifted ? '~' : '`';
    if (scancode == 0x2B) return shifted ? '|' : '\\';
    if (scancode == 0x39) return ' ';
    for (int row = 0; row < 4; row++) {
        const char *keys = shifted ? layout->shifted[row] : layout->plain[row];
        unsigned first = layout_row_first_scancode[row];
        if (scancode >= first && scancode < first + (unsigned)lstrlenA(keys)) return (unsigned char)keys[scancode - first];
    }
    return 0;
}

static unsigned scancode_for_usage(unsigned char usage) {
    for (int i = 0; i < (int)(sizeof usage_scancodes / sizeof usage_scancodes[0]); i++) {
        if (usage_scancodes[i].usage == usage) return usage_scancodes[i].scancode;
    }
    return 0;
}

static void __cdecl build_key_usage_table_hook(void) {
    original_build_key_usage_table();
    for (int i = (int)(sizeof usage_scancodes / sizeof usage_scancodes[0]) - 1; i >= 0; i--) {
        SCANCODE_TO_USAGE[usage_scancodes[i].scancode] = usage_scancodes[i].usage;
    }
    log_line("input: keys mapped by position");
}

static int ascii_from_layout(unsigned scancode, int shifted) {
    HKL layout = GetKeyboardLayout(0);
    UINT virtual_key = MapVirtualKeyExW(scancode, MAPVK_VSC_TO_VK_EX, layout);
    if (!virtual_key) return 0;
    BYTE keys[256] = {0};
    if (shifted) keys[VK_SHIFT] = keys[VK_LSHIFT] = PRESSED;
    WCHAR text[4];
    int length = ToUnicodeEx(virtual_key, scancode, keys, text, 4, TO_UNICODE_KEEP_KEYBOARD_STATE, layout);
    if (length < 0) length = 1;
    if (length != 1) return 0;
    if (text[0] > LAST_PRINTABLE) {
        WCHAR folded[4];
        if (FoldStringW(MAP_COMPOSITE, text, 1, folded, 4) < 1) return 0;
        text[0] = folded[0];
    }
    return text[0] >= FIRST_PRINTABLE && text[0] <= LAST_PRINTABLE ? text[0] : 0;
}

static unsigned char character_from_layout(unsigned char key, unsigned char modifiers, unsigned char original) {
    if (layout_mode == LAYOUT_JAPANESE) return original;
    int shifted_letter_code = original == SHIFTED_E_CODE || original == SHIFTED_Z_CODE;
    if (!shifted_letter_code && (original < FIRST_PRINTABLE || (original > LAST_PRINTABLE && original != NO_CHARACTER))) {
        return original;
    }
    unsigned scancode = scancode_for_usage(key);
    int shifted = (modifiers & SHIFT_MODIFIERS) != 0;
    int character = !scancode ? 0
                    : layout_mode == LAYOUT_TABLE ? ascii_from_layout_table(chosen_layout, scancode, shifted)
                                                  : ascii_from_layout(scancode, shifted);
    if (shifted_letter_code && character == (original == SHIFTED_E_CODE ? 'E' : 'Z')) return original;
    return character ? (unsigned char)character : original;
}

static int key_physically_down(unsigned char key) {
    if (online_lockstep_active() || replay_active()) return 1;
    unsigned scancode = scancode_for_usage(key);
    UINT virtual_key = scancode ? MapVirtualKeyExW(scancode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0)) : 0;
    return virtual_key && (GetAsyncKeyState((int)virtual_key) & KEY_DOWN_BIT);
}

static void remember_key_case(unsigned char key, unsigned char character) {
    if (!key_physically_down(key)) {
        last_key_letter = 0;
        last_key_case = WORD_CASE_ANY;
    } else if (character >= 'A' && character <= 'Z') {
        last_key_letter = (char)character;
        last_key_case = WORD_CASE_UPPER;
    } else if (character >= 'a' && character <= 'z') {
        last_key_letter = (char)(character - 'a' + 'A');
        last_key_case = WORD_CASE_LOWER;
    } else {
        last_key_letter = 0;
        last_key_case = WORD_CASE_ANY;
    }
}

static unsigned char __cdecl key_to_character_hook(unsigned char key, unsigned char modifiers) {
    unsigned char character = character_from_layout(key, modifiers, original_key_to_character(key, modifiers));
    remember_key_case(key, character == SHIFTED_E_CODE ? 'E' : character == SHIFTED_Z_CODE ? 'Z' : character);
    return character;
}

static int original_unit_index(const uint16_t *working_copy, int current, const uint16_t *reading) {
    int original = 0;
    for (int i = 0; i < current && original < WORD_MAX_READING_UNITS; i++, original++) {
        if (working_copy[i] != reading[original]) i++;
    }
    return original < WORD_MAX_READING_UNITS && working_copy[current] == reading[original] ? original : -1;
}

static int expected_letter_case(const int *typing_state) {
    const int *word = (const int *)typing_state[0];
    if (!word || !word[1]) return WORD_CASE_ANY;
    const uint16_t *reading = (const uint16_t *)word[1];
    const uint8_t *cases = dictionary_letter_cases(reading);
    const uint16_t *working_copy = (const uint16_t *)(typing_state + TYPING_STATE_READING);
    const uint16_t *current = (const uint16_t *)typing_state[TYPING_STATE_CURRENT_UNIT];
    if (!cases || current < working_copy || current >= working_copy + WORD_MAX_READING_UNITS) return WORD_CASE_ANY;
    int index = original_unit_index(working_copy, (int)(current - working_copy), reading);
    return index >= 0 ? cases[index] : WORD_CASE_ANY;
}

static const char *case_name(int letter_case) {
    return letter_case == WORD_CASE_UPPER ? "capital" : letter_case == WORD_CASE_LOWER ? "small" : "any";
}

static char expected_romaji_key(const int *typing_state) {
    const unsigned char *word = (const unsigned char *)typing_state[0];
    return word ? (char)word[WORD_ROMAJI_TEXT + word[WORD_ROMAJI_POSITION]] : 0;
}

static unsigned char __cdecl judge_key_hook(int *typing_state, char character) {
    int typed_case = character == last_key_letter ? last_key_case : WORD_CASE_ANY;
    last_key_letter = 0;
    last_key_case = WORD_CASE_ANY;
    if (autotype && typing_state && typed_case != WORD_CASE_ANY && expected_romaji_key(typing_state)) {
        const int *word = (const int *)typing_state[0];
        if (word[1] && dictionary_quiz_answer((const uint16_t *)word[1]) == 0) {
            char wrong = expected_romaji_key(typing_state) == 'X' ? 'Q' : 'X';
            return original_judge_key(typing_state, wrong);
        }
        return original_judge_key(typing_state, expected_romaji_key(typing_state));
    }
    int expected = typing_state && check_letter_case ? expected_letter_case(typing_state) : WORD_CASE_ANY;
    int wrong_case = typed_case != WORD_CASE_ANY && expected != WORD_CASE_ANY && expected != typed_case;
    unsigned char result = original_judge_key(typing_state, wrong_case ? (character == 'X' ? 'Q' : 'X') : character);
    if (log_typing && character >= 'A' && character <= 'Z') {
        log_line("typing: '%c' typed %s, word wants %s -> %s (result %02X)", character, case_name(typed_case),
                 case_name(expected), wrong_case ? "miss (wrong case)" : "judged by the game", result);
    }
    return result;
}

static void __cdecl word_box_task_hook(void *task) {
    unsigned char swapped[PLAYER_KEY_SLOTS] = {0};
    for (int player = 0; player < PLAYER_KEY_SLOTS; player++) {
        volatile unsigned char *newest = NEWEST_KEY(player);
        if (*newest == USAGE_BACKSPACE) {
            *newest = USAGE_ESCAPE;
            swapped[player] = 1;
        }
    }
    original_word_box_task(task);
    for (int player = 0; player < PLAYER_KEY_SLOTS; player++) {
        if (swapped[player] && *NEWEST_KEY(player) == USAGE_ESCAPE) *NEWEST_KEY(player) = USAGE_BACKSPACE;
    }
}

static MH_STATUS hook_function(DWORD address, void *hook, void **original) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, original);
    return status == MH_OK ? MH_EnableHook((void *)address) : status;
}

void install_input_hooks(const char *ini_path) {
    char layout_setting[16];
    GetPrivateProfileStringA("input", "keyboard_layout", "windows", layout_setting, sizeof layout_setting, ini_path);
    if (GetPrivateProfileIntA("input", "backspace_cancels", 1, ini_path)) {
        log_line("input: Backspace also cancels the word being typed (hook word_box_task: %s)",
                 MH_StatusToString(hook_function(WORD_BOX_TASK_VA, word_box_task_hook, (void **)&original_word_box_task)));
    }
    check_letter_case = GetPrivateProfileIntA("typing", "case_sensitive", 0, ini_path);
    autotype = GetPrivateProfileIntA("debug", "autotype", 0, ini_path);
    log_typing = GetPrivateProfileIntA("debug", "log_typing", 0, ini_path);
    char layout_name[KL_NAMELENGTH] = "";
    GetKeyboardLayoutNameA(layout_name);
    int japanese_windows_layout = PRIMARYLANGID(LOWORD((DWORD_PTR)GetKeyboardLayout(0))) == LANG_JAPANESE;
    chosen_layout = find_keyboard_layout(layout_setting);
    layout_mode = chosen_layout ? LAYOUT_TABLE
                  : lstrcmpiA(layout_setting, "japanese") == 0 || japanese_windows_layout ? LAYOUT_JAPANESE
                                                                                         : LAYOUT_WINDOWS;
    if (layout_mode != LAYOUT_JAPANESE) {
        const char *hook_status = MH_StatusToString(
            hook_function(BUILD_KEY_USAGE_TABLE_VA, build_key_usage_table_hook, (void **)&original_build_key_usage_table));
        if (layout_mode == LAYOUT_TABLE) {
            log_line("input: keys use the %s layout by key position, whatever the Windows layout (%s) "
                     "(hook build_key_usage_table: %s)", chosen_layout->name, layout_name, hook_status);
        } else {
            log_line("input: keys follow the Windows keyboard layout %s (hook build_key_usage_table: %s)", layout_name,
                     hook_status);
        }
    } else {
        log_line("input: keys use the game's built-in Japanese layout (Windows layout %s)", layout_name);
    }
    if (layout_mode != LAYOUT_JAPANESE || check_letter_case || autotype) {
        log_line("input: hook key_to_character: %s",
                 MH_StatusToString(hook_function(KEY_TO_CHARACTER_VA, key_to_character_hook, (void **)&original_key_to_character)));
    }
    if (check_letter_case || autotype) {
        log_line("input: %s%s (hook judge_key: %s)", check_letter_case ? "capital letters must match" : "",
                 autotype ? " [debug] autotype: any letter key types the next letter" : "",
                 MH_StatusToString(hook_function(JUDGE_KEY_VA, judge_key_hook, (void **)&original_judge_key)));
    }
}
