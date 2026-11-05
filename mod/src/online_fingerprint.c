#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "online_fingerprint.h"

#define FNV_OFFSET 0x811c9dc5u
#define FNV_PRIME 0x01000193u
#define OTHER_SECTION "\x82\xbb\x82\xcc\x91\xbc"

enum { PART_VERSION, PART_WORDS, PART_TYPING, PART_RULES, PART_GAME_SETTINGS, PART_MODELS };

static const char *part_names[FINGERPRINT_PARTS] = {
    "todmod version",
    "word packs ([dictionary], dict\\ files, word.HAB)",
    "typing and keyboard ([input], [typing], [text] language/half_width, [debug] autotype, keyboard layout)",
    "rules ([randomizer], [cheats], [mutators], [gameplay])",
    "game settings (Tod.ini GameSpec, sound switches, hidden commands)",
    "model packs ([models] packs)",
};

const char *online_fingerprint_part_name(int part) {
    return part >= 0 && part < FINGERPRINT_PARTS ? part_names[part] : "?";
}

static uint32_t hash_bytes(uint32_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * FNV_PRIME;
    return hash;
}

static uint32_t hash_text(uint32_t hash, const char *text) {
    return hash_bytes(hash, text, strlen(text) + 1);
}

static uint32_t hash_file(uint32_t hash, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return hash_text(hash, "missing");
    unsigned char buffer[65536];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, file)) > 0) hash = hash_bytes(hash, buffer, got);
    fclose(file);
    return hash;
}

static uint32_t hash_ini_value(uint32_t hash, const char *ini_path, const char *section, const char *key,
                               const char *fallback) {
    char value[512];
    GetPrivateProfileStringA(section, key, fallback, value, sizeof value, ini_path);
    CharLowerA(value);
    hash = hash_text(hash, key);
    return hash_text(hash, value);
}

static uint32_t hash_ini_section(uint32_t hash, const char *ini_path, const char *section) {
    char lines[8192];
    DWORD length = GetPrivateProfileSectionA(section, lines, sizeof lines, ini_path);
    hash = hash_text(hash, section);
    return hash_bytes(hash, lines, length);
}

static void folder_of(const char *path, char *folder) {
    strcpy(folder, path);
    char *slash = strrchr(folder, '\\');
    if (slash) slash[1] = '\0';
}

static uint32_t hash_word_packs(const char *ini_path) {
    char folder[MAX_PATH], packs[512], path[MAX_PATH];
    folder_of(ini_path, folder);
    uint32_t hash = FNV_OFFSET;
    hash = hash_ini_value(hash, ini_path, "dictionary", "packs", "");
    hash = hash_ini_value(hash, ini_path, "dictionary", "mix", "0");
    hash = hash_ini_value(hash, ini_path, "dictionary", "in_order", "0");
    hash = hash_ini_value(hash, ini_path, "dictionary", "in_order_min_length", "4");
    GetPrivateProfileStringA("dictionary", "packs", "", packs, sizeof packs, ini_path);
    static const char *pack_files[] = {"words.tsv", "kids.tsv", "sequence.txt", "sequence_position.txt"};
    for (char *pack = strtok(packs, ","); pack; pack = strtok(NULL, ",")) {
        while (*pack == ' ') pack++;
        for (size_t i = 0; i < sizeof pack_files / sizeof pack_files[0]; i++) {
            _snprintf(path, sizeof path, "%sdict\\%s\\%s", folder, pack, pack_files[i]);
            path[sizeof path - 1] = '\0';
            hash = hash_file(hash, path);
        }
    }
    _snprintf(path, sizeof path, "%sword.HAB", folder);
    path[sizeof path - 1] = '\0';
    return hash_file(hash, path);
}

static uint32_t hash_typing(const char *ini_path) {
    uint32_t hash = FNV_OFFSET;
    char layout[32];
    GetPrivateProfileStringA("input", "keyboard_layout", "windows", layout, sizeof layout, ini_path);
    hash = hash_text(hash, CharLowerA(layout));
    if (!lstrcmpiA(layout, "windows")) {
        char windows_layout[KL_NAMELENGTH] = "";
        GetKeyboardLayoutNameA(windows_layout);
        hash = hash_text(hash, windows_layout);
    }
    hash = hash_ini_value(hash, ini_path, "input", "backspace_cancels", "1");
    hash = hash_ini_section(hash, ini_path, "typing");
    hash = hash_ini_value(hash, ini_path, "text", "language", "japanese");
    hash = hash_ini_value(hash, ini_path, "text", "half_width", "0");
    return hash_ini_value(hash, ini_path, "debug", "autotype", "0");
}

static uint32_t hash_rules(const char *ini_path) {
    uint32_t hash = FNV_OFFSET;
    hash = hash_ini_section(hash, ini_path, "randomizer");
    hash = hash_ini_section(hash, ini_path, "cheats");
    hash = hash_ini_section(hash, ini_path, "mutators");
    return hash_ini_section(hash, ini_path, "gameplay");
}

static uint32_t hash_game_settings(void) {
    char path[MAX_PATH] = "";
    GetEnvironmentVariableA("APPDATA", path, MAX_PATH);
    strncat(path, "\\Sega\\TODEX\\Tod.ini", MAX_PATH - strlen(path) - 1);
    uint32_t hash = FNV_OFFSET;
    static const char *keys[] = {"GameSpec", "Sound", "BGM", "SE", "Voice"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        hash = hash_ini_value(hash, path, "HGLCORE", keys[i], "");
    return hash_ini_section(hash, path, OTHER_SECTION);
}

void online_fingerprint(const char *ini_path, const char *version, uint32_t parts[FINGERPRINT_PARTS]) {
    parts[PART_VERSION] = hash_text(FNV_OFFSET, version);
    parts[PART_WORDS] = hash_word_packs(ini_path);
    parts[PART_TYPING] = hash_typing(ini_path);
    parts[PART_RULES] = hash_rules(ini_path);
    parts[PART_GAME_SETTINGS] = hash_game_settings();
    parts[PART_MODELS] = hash_ini_value(FNV_OFFSET, ini_path, "models", "packs", "");
}
