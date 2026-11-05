#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ini_file.h"
#include "pack_builder.h"
#include "pack_coverage.h"
#include "word_pack.h"
#include "resource.h"
#include "character_list.h"
#include "character_page.h"
#include "subtitle_page.h"
#include "stats_page.h"
#include "online_page.h"
#include "mutators_page.h"
#include "replays_page.h"
#include "audio_page.h"

#define NORMAL_CHARACTER (-1)
#define PACK_DESCRIPTION_BYTES 1024

static int start_on_characters;
static int start_on_subtitles;
static int start_on_randomizer;
static int start_on_online;
static int start_on_mutators;
static int start_on_stats;
static int start_on_replays;
static int start_on_audio;
static int start_character = NORMAL_CHARACTER;

#define HGLCORE "HGLCORE"
#define MAX_PACK_LIST 1024
#define MIN_DIMENSION 320
#define MAX_DIMENSION 7680
#define MIN_CAP_HEIGHT 10
#define MAX_CAP_HEIGHT 24
#define NATIVE_CAP_HEIGHT 20

typedef struct {
    int width, height;
} resolution;

static const resolution original_resolution_table[] = {
    {640, 480}, {320, 200}, {320, 240}, {400, 300}, {480, 360}, {512, 384}, {640, 400}, {800, 600},
    {1024, 768}, {1152, 864}, {1280, 960}, {1280, 1024}, {1600, 900}, {1600, 1024}, {1600, 1200}};
static const resolution preset_resolutions[] = {
    {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {1280, 800},
    {1440, 900}, {1680, 1050}, {1920, 1200}, {2560, 1600}, {2560, 1080}, {3440, 1440}, {640, 480},
    {800, 600}, {1024, 768}, {1280, 960}, {1600, 1200}};
static const char *const filter_names[] = {"Some textures (original)", "Off", "All textures"};
static const char *const detail_names[] = {"Lowest", "Low", "Normal", "High", "Highest"};

static char game_folder[MAX_PATH], tod_ini_path[MAX_PATH], todmod_ini_path[MAX_PATH], dict_folder[MAX_PATH], textures_folder[MAX_PATH], music_folder[MAX_PATH], models_folder[MAX_PATH], voices_folder[MAX_PATH];
static ini_file tod_ini, todmod_ini;
static resolution desktop;

typedef struct {
    const char *setting;
    const char *label;
} keyboard_layout_choice;

static const keyboard_layout_choice keyboard_layouts[] = {
    {"windows", "As set in Windows"},
    {"qwerty", "US QWERTY"},
    {"dvorak", "Dvorak"},
    {"colemak", "Colemak"},
    {"workman", "Workman"},
    {"japanese", "Japanese (the game's own)"},
};
#define KEYBOARD_LAYOUT_COUNT ((int)(sizeof keyboard_layouts / sizeof keyboard_layouts[0]))

static const keyboard_layout_choice suits[] = {
    {"none", "None (normal players)"},
    {"zombie", "Zombie suit"},
    {"elderly", "Old man / old woman suit"},
};
#define SUIT_COUNT ((int)(sizeof suits / sizeof suits[0]))

static const char todmod_ini_template[] =
    "[dictionary]\r\n"
    "; Packs in dict\\, highest priority first, separated by commas. Items missing from every pack come from word.HAB.\r\n"
    "packs=\r\n"
    "; 1 = compare each pack item with the same word.HAB item and log differences (debugging)\r\n"
    "verify=0\r\n"
    "; 1 = a list's words come from every pack that has it (mixed); 0 = the first pack with the list wins\r\n"
    "mix=0\r\n"
    "; 1 = words come in file order from the top pack's sequence.txt (else its words.tsv); words the game picks\r\n"
    "; that are shorter than in_order_min_length keys stay as they are (letters, numbers, short words)\r\n"
    "in_order=0\r\n"
    "in_order_min_length=4\r\n"
    "\r\n"
    "[text]\r\n"
    "; GDI font for characters the game font lacks (font linking supplies missing scripts, e.g. Hangul)\r\n"
    "font=Arial\r\n"
    "weight=400\r\n"
    "; capital letter height in pixels (the game's own letters: 20; glyph cell: 24)\r\n"
    "cap_height=20\r\n"
    "; 1 = all Latin/Greek/Cyrillic words use narrow (half-width) text; 0 = only lines too long for full width\r\n"
    "half_width=1\r\n"
    "half_width_font=Consolas\r\n"
    "; english = the game's English mode (English subtitles from the game's own text); japanese = original\r\n"
    "language=japanese\r\n"
    "; subtitle file in subtitles\\ (without .tsv) replacing the cutscene subtitles; empty = original\r\n"
    "subtitles=\r\n"
    "; Windows font for subtitle lines the game font cannot show (Japanese, Cyrillic ...)\r\n"
    "subtitle_font=Meiryo\r\n"
    "; 1 = draw all subtitles (also the game's English text) with subtitle_font\r\n"
    "subtitle_windows_font=0\r\n"
    "; 1 = keep each subtitle up until its voice line has finished\r\n"
    "subtitles_cover_voice=1\r\n"
    "\r\n"
    "[typing]\r\n"
    "; 1 = capital letters in pack words must be typed with Shift (and small letters without)\r\n"
    "case_sensitive=0\r\n"
    "; 1 = with case_sensitive, each capital letter adds uppercase_points to the word's difficulty (score)\r\n"
    "case_scoring=1\r\n"
    "uppercase_points=4\r\n"
    "; 1 = spaces in pack words must be typed (as in the original); 0 = skip them (word scores unchanged)\r\n"
    "type_spaces=1\r\n"
    "\r\n"
    "[input]\r\n"
    "; windows = typed characters follow the Windows keyboard layout; qwerty, dvorak, colemak, workman = that\r\n"
    "; layout by key position whatever Windows is set to; japanese = the game's own JP106 layout\r\n"
    "keyboard_layout=windows\r\n"
    "; 1 = Backspace also cancels the word being typed (like Esc)\r\n"
    "backspace_cancels=1\r\n"
    "\r\n"
    "[gameplay]\r\n"
    "; 1 = skip every skippable cutscene (the ones showing \"Press Enter to skip\") as if Enter was pressed\r\n"
    "skip_cutscenes=0\r\n"
    "; 1 = Esc during play opens a pause menu (Resume, Restart stage, Exit to menu)\r\n"
    "pause_menu=1\r\n"
    "\r\n"
    "[cheats]\r\n"
    "; 1 = everyone has a big head all the time (the Balloon Head item's effect)\r\n"
    "big_heads=0\r\n"
    "; 1 = hits still knock you back but never cost a life\r\n"
    "infinite_lives=0\r\n"
    "; none, zombie or elderly = players always wear that skin suit (as the suit items do; applies when a stage loads)\r\n"
    "suit=none\r\n"
    "; character kind (0-87, -1 = normal) each player is shown as: zombies, civilians, bosses ...\r\n"
    "player1_character=-1\r\n"
    "player2_character=-1\r\n"
    "\r\n"
    "[randomizer]\r\n"
    "; 1 = randomize each game; seed 0 = a new mix every launch (the seed used is written to todmod.log)\r\n"
    "enabled=0\r\n"
    "seed=0\r\n"
    "; zombie types look like other zombie types; civilians/story characters like other people; mix = across both\r\n"
    "enemies=1\r\n"
    "people=1\r\n"
    "mix=0\r\n"
    "; scenes speak other lines (subtitles follow); picked-up items have a random effect\r\n"
    "voices=1\r\n"
    "items=1\r\n"
    "; thrown weapons become other weapons\r\n"
    "weapons=1\r\n"
    "; percent of zombie types drawn as people (0, 25, 50, 100)\r\n"
    "human_zombies=0\r\n"
    "; each zombie appears enemy_min to enemy_max times (0-4; 0 = may stay away; bosses never doubled)\r\n"
    "enemy_min=1\r\n"
    "enemy_max=1\r\n"
    "\r\n"
    "[mutators]\r\n"
    "; most lives (0 = normal); 1 = a typo costs a life\r\n"
    "max_lives=0\r\n"
    "typos_hurt=0\r\n"
    "; word difficulty: normal | easiest | hardest\r\n"
    "word_level=normal\r\n"
    "; pack words: normal | reversed | scrambled; letter_case: normal | upper | lower | random\r\n"
    "words=normal\r\n"
    "letter_case=normal\r\n"
    "; game speed in percent (50-200)\r\n"
    "speed=100\r\n"
    "\r\n"
    "[stats]\r\n"
    "; 1 = write typing statistics of every game to stats\\ (history.tsv, a report per game, practice_words.txt)\r\n"
    "enabled=1\r\n"
    "\r\n"
    "[replay]\r\n"
    "; 1 = record every game to replays\\ (keys only; the newest 'keep' files stay)\r\n"
    "record=1\r\n"
    "keep=20\r\n"
    "; a replay file in replays\\ to watch at the next start (or set TODMOD_REPLAY)\r\n"
    "play=\r\n"
    "\r\n"
    "[online]\r\n"
    "; Experimental online play: off | host (player 1) | join (player 2); TODMOD_ONLINE overrides it per copy\r\n"
    "mode=off\r\n"
    "; host's address (join), UDP port (the host forwards it), or a relay server name:port plus a room name\r\n"
    "peer=127.0.0.1\r\n"
    "port=7845\r\n"
    "relay=\r\n"
    "room=\r\n"
    "; frames between a key press and its effect: auto (from the ping) or 0-15\r\n"
    "input_delay=auto\r\n"
    "; 1 = write todmod_online_<role>.tsv (per-frame checks) and todmod_rand_<role>.tsv (random number callers)\r\n"
    "log_hashes=0\r\n"
    "log_rand_callers=0\r\n"
    "\r\n"
    "[models]\r\n"
    "; Model packs in models\\ (models\\<pack>\\<bank>\\<model id>.obj or .bin), highest priority first\r\n"
    "packs=\r\n"
    "; 1 = save every loaded model to models\\dump\\<bank>\\<model id>.obj/.mtl/.bin\r\n"
    "dump=0\r\n"
    "\r\n"
    "[debug]\r\n"
    "; 1 = list every loaded texture / model in todmod_textures.log / todmod_models.log (tab-separated)\r\n"
    "log_textures=0\r\n"
    "log_models=0\r\n"
    "\r\n"
    "[display]\r\n"
    "; Render size in pixels; 0 = use the Configurater Resolution setting\r\n"
    "width=0\r\n"
    "height=0\r\n"
    "; 1 = 3D fills the width (Hor+) and 2D stays 4:3 centred; 0 = stretch everything like the original\r\n"
    "widescreen=1\r\n"
    "; 1 = a borderless window that covers the screen (quick Alt+Tab; width/height 0 = the screen's size)\r\n"
    "borderless=0\r\n"
    "; blood colour: green (this game) or red (as in The House of the Dead 2)\r\n"
    "blood=green\r\n"
    "\r\n"
    "[timing]\r\n"
    "; timer = sleep most of each frame wait (low CPU); spin = original busy-wait\r\n"
    "limiter=timer\r\n"
    "; 1 = draw blended in-between frames on monitors faster than 60 Hz (experimental)\r\n"
    "frame_interpolation=0\r\n"
    "; game updates per second: 60 = normal speed (30 = half-speed test of the in-between frames)\r\n"
    "update_rate=60\r\n"
    "; 1 = log frames/s, longest frame and CPU use every 10 s\r\n"
    "stats=0\r\n";

static void join_path(char *out, const char *folder, const char *name) {
    snprintf(out, MAX_PATH, "%s%s", folder, name);
}

static void resolve_paths(void) {
    GetModuleFileNameA(NULL, game_folder, MAX_PATH);
    char *slash = strrchr(game_folder, '\\');
    if (slash) slash[1] = '\0';
    join_path(todmod_ini_path, game_folder, "todmod.ini");
    join_path(dict_folder, game_folder, "dict");
    join_path(textures_folder, game_folder, "textures");
    join_path(music_folder, game_folder, "music");
    join_path(models_folder, game_folder, "models");
    join_path(voices_folder, game_folder, "voices");

    char app_data[MAX_PATH], user_folder[MAX_PATH], default_ini[MAX_PATH];
    SHGetSpecialFolderPathA(NULL, app_data, CSIDL_APPDATA, TRUE);
    snprintf(user_folder, MAX_PATH, "%s\\Sega", app_data);
    CreateDirectoryA(user_folder, NULL);
    snprintf(user_folder, MAX_PATH, "%s\\Sega\\TODEX\\", app_data);
    CreateDirectoryA(user_folder, NULL);
    join_path(tod_ini_path, user_folder, "Tod.ini");
    join_path(default_ini, game_folder, "Default.ini");
    CopyFileA(default_ini, tod_ini_path, TRUE);
    SetFileAttributesA(tod_ini_path, FILE_ATTRIBUTE_NORMAL);
}

static void load_todmod_ini(void) {
    if (ini_load(&todmod_ini, todmod_ini_path)) return;
    FILE *file = fopen(todmod_ini_path, "wb");
    if (file) {
        fwrite(todmod_ini_template, 1, sizeof todmod_ini_template - 1, file);
        fclose(file);
    }
    ini_load(&todmod_ini, todmod_ini_path);
}

static void set_status(HWND dialog, const char *text) {
    SetDlgItemTextA(dialog, IDC_STATUS, text);
}

static void add_combo_item(HWND dialog, int control, const char *text, LPARAM data) {
    LRESULT index = SendDlgItemMessageA(dialog, control, CB_ADDSTRING, 0, (LPARAM)text);
    SendDlgItemMessageA(dialog, control, CB_SETITEMDATA, index, data);
}

static LPARAM selected_combo_data(HWND dialog, int control) {
    LRESULT index = SendDlgItemMessageA(dialog, control, CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? -1 : SendDlgItemMessageA(dialog, control, CB_GETITEMDATA, index, 0);
}

static void select_combo_data(HWND dialog, int control, LPARAM data) {
    LRESULT count = SendDlgItemMessageA(dialog, control, CB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; i++) {
        if (SendDlgItemMessageA(dialog, control, CB_GETITEMDATA, i, 0) == data) {
            SendDlgItemMessageA(dialog, control, CB_SETCURSEL, i, 0);
            return;
        }
    }
}

#define RESOLUTION_DESKTOP (-1)
#define RESOLUTION_CUSTOM (-2)

static void fill_resolution_list(HWND dialog) {
    char text[64];
    snprintf(text, sizeof text, "Desktop (%d x %d)", desktop.width, desktop.height);
    add_combo_item(dialog, IDC_RESOLUTION, text, RESOLUTION_DESKTOP);
    for (int i = 0; i < (int)(sizeof preset_resolutions / sizeof preset_resolutions[0]); i++) {
        resolution r = preset_resolutions[i];
        snprintf(text, sizeof text, "%d x %d%s", r.width, r.height, r.width * 3 == r.height * 4 ? "  (4:3)" : "");
        add_combo_item(dialog, IDC_RESOLUTION, text, i);
    }
    add_combo_item(dialog, IDC_RESOLUTION, "Custom", RESOLUTION_CUSTOM);
}

static void update_custom_size_fields(HWND dialog) {
    BOOL custom = selected_combo_data(dialog, IDC_RESOLUTION) == RESOLUTION_CUSTOM;
    EnableWindow(GetDlgItem(dialog, IDC_WIDTH), custom);
    EnableWindow(GetDlgItem(dialog, IDC_HEIGHT), custom);
}

static void show_resolution(HWND dialog, resolution r) {
    LPARAM choice = RESOLUTION_CUSTOM;
    if (r.width == desktop.width && r.height == desktop.height) choice = RESOLUTION_DESKTOP;
    for (int i = 0; choice == RESOLUTION_CUSTOM && i < (int)(sizeof preset_resolutions / sizeof preset_resolutions[0]); i++) {
        if (preset_resolutions[i].width == r.width && preset_resolutions[i].height == r.height) choice = i;
    }
    select_combo_data(dialog, IDC_RESOLUTION, choice);
    SetDlgItemInt(dialog, IDC_WIDTH, r.width, FALSE);
    SetDlgItemInt(dialog, IDC_HEIGHT, r.height, FALSE);
    update_custom_size_fields(dialog);
}

static int chosen_resolution(HWND dialog, resolution *r) {
    LPARAM choice = selected_combo_data(dialog, IDC_RESOLUTION);
    if (choice == RESOLUTION_DESKTOP) {
        *r = desktop;
    } else if (choice >= 0) {
        *r = preset_resolutions[choice];
    } else {
        r->width = GetDlgItemInt(dialog, IDC_WIDTH, NULL, FALSE);
        r->height = GetDlgItemInt(dialog, IDC_HEIGHT, NULL, FALSE);
    }
    return r->width >= MIN_DIMENSION && r->width <= MAX_DIMENSION && r->height >= MIN_DIMENSION / 2 &&
           r->height <= MAX_DIMENSION;
}

static resolution configured_resolution(void) {
    resolution r = {ini_get_int(&todmod_ini, "display", "width", 0), ini_get_int(&todmod_ini, "display", "height", 0)};
    if (r.width > 0 && r.height > 0) return r;
    int index = ini_get_int(&tod_ini, HGLCORE, "Resolution", 0);
    if (index < 0 || index >= (int)(sizeof original_resolution_table / sizeof original_resolution_table[0])) index = 0;
    return original_resolution_table[index];
}

static int original_resolution_index(resolution r) {
    for (int i = 0; i < (int)(sizeof original_resolution_table / sizeof original_resolution_table[0]); i++) {
        if (original_resolution_table[i].width == r.width && original_resolution_table[i].height == r.height) return i;
    }
    return -1;
}

static void list_model_pack_folders(HWND dialog) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s\\*", models_folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.' ||
            !_stricmp(found.cFileName, "dump")) {
            continue;
        }
        SendDlgItemMessageA(dialog, IDC_MODEL_PACKS, CB_ADDSTRING, 0, (LPARAM)found.cFileName);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static int CALLBACK add_font_family(const LOGFONTW *font, const TEXTMETRICW *metrics, DWORD type, LPARAM dialog) {
    (void)metrics;
    (void)type;
    if (font->lfFaceName[0] == L'@') return 1;
    HWND combo = GetDlgItem((HWND)dialog, IDC_FONT);
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)font->lfFaceName) == CB_ERR) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)font->lfFaceName);
    }
    return 1;
}

static void fill_font_list(HWND dialog) {
    LOGFONTW query = {0};
    query.lfCharSet = DEFAULT_CHARSET;
    HDC dc = GetDC(dialog);
    EnumFontFamiliesExW(dc, &query, add_font_family, (LPARAM)dialog, 0);
    ReleaseDC(dialog, dc);
}

static void select_font(HWND dialog, const char *face) {
    wchar_t wide[LF_FACESIZE];
    MultiByteToWideChar(CP_ACP, 0, face, -1, wide, LF_FACESIZE);
    HWND combo = GetDlgItem(dialog, IDC_FONT);
    LRESULT index = SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wide);
    if (index == CB_ERR) index = SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)L"Arial");
    SendMessageW(combo, CB_SETCURSEL, index == CB_ERR ? 0 : index, 0);
}

static int list_contains(HWND list, const char *name) {
    return SendMessageA(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)name) != LB_ERR;
}

static void fill_pack_lists(HWND dialog) {
    HWND available = GetDlgItem(dialog, IDC_PACKS_AVAILABLE), active = GetDlgItem(dialog, IDC_PACKS_ACTIVE);
    char packs[MAX_PACK_LIST];
    ini_get(&todmod_ini, "dictionary", "packs", packs, sizeof packs);
    for (char *name = strtok(packs, ","); name; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (*name && !list_contains(active, name)) SendMessageA(active, LB_ADDSTRING, 0, (LPARAM)name);
    }
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*", dict_folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.') continue;
        if (!list_contains(active, found.cFileName)) SendMessageA(available, LB_ADDSTRING, 0, (LPARAM)found.cFileName);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static void move_selected_pack(HWND dialog, int from_control, int to_control) {
    HWND from = GetDlgItem(dialog, from_control), to = GetDlgItem(dialog, to_control);
    LRESULT index = SendMessageA(from, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) return;
    char name[MAX_PATH];
    SendMessageA(from, LB_GETTEXT, index, (LPARAM)name);
    SendMessageA(from, LB_DELETESTRING, index, 0);
    LRESULT added = SendMessageA(to, LB_ADDSTRING, 0, (LPARAM)name);
    SendMessageA(to, LB_SETCURSEL, added, 0);
    LRESULT remaining = SendMessageA(from, LB_GETCOUNT, 0, 0);
    if (remaining) SendMessageA(from, LB_SETCURSEL, index < remaining ? index : remaining - 1, 0);
}

static void shift_active_pack(HWND dialog, int direction) {
    HWND active = GetDlgItem(dialog, IDC_PACKS_ACTIVE);
    LRESULT index = SendMessageA(active, LB_GETCURSEL, 0, 0);
    LRESULT target = index + direction;
    if (index == LB_ERR || target < 0 || target >= SendMessageA(active, LB_GETCOUNT, 0, 0)) return;
    char name[MAX_PATH];
    SendMessageA(active, LB_GETTEXT, index, (LPARAM)name);
    SendMessageA(active, LB_DELETESTRING, index, 0);
    SendMessageA(active, LB_INSERTSTRING, target, (LPARAM)name);
    SendMessageA(active, LB_SETCURSEL, target, 0);
}

static void active_pack_list(HWND dialog, char *out, size_t out_size) {
    HWND active = GetDlgItem(dialog, IDC_PACKS_ACTIVE);
    out[0] = '\0';
    LRESULT count = SendMessageA(active, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; i++) {
        char name[MAX_PATH];
        SendMessageA(active, LB_GETTEXT, i, (LPARAM)name);
        if (i) strncat(out, ",", out_size - strlen(out) - 1);
        strncat(out, name, out_size - strlen(out) - 1);
    }
}

static void show_pack_description(HWND dialog, int list_control) {
    HWND list = GetDlgItem(dialog, list_control);
    LRESULT index = SendMessageA(list, LB_GETCURSEL, 0, 0);
    char name[MAX_PATH] = "", path[MAX_PATH], text[PACK_DESCRIPTION_BYTES] = "";
    if (index == LB_ERR || SendMessageA(list, LB_GETTEXTLEN, (WPARAM)index, 0) >= MAX_PATH) return;
    SendMessageA(list, LB_GETTEXT, (WPARAM)index, (LPARAM)name);
    snprintf(path, sizeof path, "%s\\%s\\description.txt", dict_folder, name);
    FILE *file = fopen(path, "rb");
    size_t length = file ? fread(text, 1, sizeof text - 1, file) : 0;
    if (file) fclose(file);
    text[length] = '\0';
    char *start = text;
    if ((unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) start += 3;
    wchar_t wide[PACK_DESCRIPTION_BYTES];
    if (*start) {
        MultiByteToWideChar(CP_UTF8, 0, start, -1, wide, PACK_DESCRIPTION_BYTES);
    } else {
        swprintf(wide, PACK_DESCRIPTION_BYTES, L"%hs: no description (write one in dict\\%hs\\description.txt).", name, name);
    }
    static wchar_t shown[PACK_DESCRIPTION_BYTES * 2];
    size_t used = 0;
    for (const wchar_t *c = wide; *c && used + 2 < sizeof shown / sizeof shown[0]; c++) {
        if (*c == 10 && (c == wide || c[-1] != 13)) shown[used++] = 13;
        shown[used++] = *c;
    }
    shown[used] = 0;
    SetDlgItemTextW(dialog, IDC_PACK_DESCRIPTION, shown);
}

static void set_check(HWND dialog, int control, int checked) {
    CheckDlgButton(dialog, control, checked ? BST_CHECKED : BST_UNCHECKED);
}

static int is_checked(HWND dialog, int control) {
    return IsDlgButtonChecked(dialog, control) == BST_CHECKED;
}

static const char *const blood_colours[] = {"green", "red", "purple", "gold", "blue", "grey", "black", "white",
                                            "pink", "orange", "cyan", "yellow", "rainbow"};
static const char *const blood_colour_labels[] = {"Green (game)", "Red (game)", "Purple", "Gold", "Blue", "Grey", "Black",
                                                  "White", "Pink", "Orange", "Cyan", "Yellow", "Rainbow"};
#define BLOOD_COLOUR_COUNT ((int)(sizeof blood_colours / sizeof blood_colours[0]))

#define FOV_MIN 25
#define FOV_MAX 100
#define GAME_FOV 41

static void show_fov_text(HWND dialog) {
    int fov = (int)SendDlgItemMessageA(dialog, IDC_FOV, TBM_GETPOS, 0, 0);
    char text[48];
    snprintf(text, sizeof text, fov == GAME_FOV ? "%d degrees (game)" : "%d degrees", fov);
    SetDlgItemTextA(dialog, IDC_FOV_TEXT, text);
}

static void show_volume_text(HWND dialog, int slider) {
    char text[16];
    snprintf(text, sizeof text, "%d%%", (int)SendDlgItemMessageA(dialog, slider, TBM_GETPOS, 0, 0));
    SetDlgItemTextA(dialog, slider + 1, text);
}

static void show_volume(HWND dialog, int slider, int percent) {
    SendDlgItemMessageA(dialog, slider, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendDlgItemMessageA(dialog, slider, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessageA(dialog, slider, TBM_SETPOS, TRUE, percent < 0 ? 0 : percent > 100 ? 100 : percent);
    show_volume_text(dialog, slider);
}

static int volume_position(HWND dialog, int slider) {
    return (int)SendDlgItemMessageA(dialog, slider, TBM_GETPOS, 0, 0);
}

static void update_case_scoring_field(HWND dialog) {
    EnableWindow(GetDlgItem(dialog, IDC_CASE_SCORING), is_checked(dialog, IDC_CASE_SENSITIVE));
}

static void show_settings(HWND dialog) {
    fill_resolution_list(dialog);
    show_resolution(dialog, configured_resolution());
    set_check(dialog, IDC_WINDOWED, ini_get_bool(&tod_ini, HGLCORE, "Window", 0));
    set_check(dialog, IDC_BORDERLESS, ini_get_int(&todmod_ini, "display", "borderless", 0));
    {
        char blood[16] = "green";
        ini_get(&todmod_ini, "display", "blood", blood, sizeof blood);
        int chosen_blood = 0;
        for (int i = 0; i < BLOOD_COLOUR_COUNT; i++) {
            add_combo_item(dialog, IDC_RED_BLOOD, blood_colour_labels[i], i);
            if (_stricmp(blood, blood_colours[i]) == 0) chosen_blood = i;
        }
        select_combo_data(dialog, IDC_RED_BLOOD, chosen_blood);
    }
    set_check(dialog, IDC_WIDESCREEN, ini_get_int(&todmod_ini, "display", "widescreen", 1));

    add_combo_item(dialog, IDC_BPP, "16-bit", 16);
    add_combo_item(dialog, IDC_BPP, "32-bit", 32);
    select_combo_data(dialog, IDC_BPP, ini_get_int(&tod_ini, HGLCORE, "Bpp", 16) == 32 ? 32 : 16);
    for (int i = 0; i < 3; i++) add_combo_item(dialog, IDC_FILTER, filter_names[i], i);
    select_combo_data(dialog, IDC_FILTER, ini_get_int(&tod_ini, HGLCORE, "Filter", 0));
    for (int i = 0; i < 5; i++) add_combo_item(dialog, IDC_GAMESPEC, detail_names[i], i);
    select_combo_data(dialog, IDC_GAMESPEC, ini_get_int(&tod_ini, HGLCORE, "GameSpec", 2));
    set_check(dialog, IDC_FOG, ini_get_bool(&tod_ini, HGLCORE, "Fog", 1));
    set_check(dialog, IDC_TRANSPARENCY, ini_get_bool(&tod_ini, HGLCORE, "Transparency", 1));
    set_check(dialog, IDC_SMALL_TEXTURES, ini_get_bool(&tod_ini, HGLCORE, "SmallTexture", 0));

    set_check(dialog, IDC_SOUND, ini_get_bool(&tod_ini, HGLCORE, "Sound", 1));
    set_check(dialog, IDC_MUSIC, ini_get_bool(&tod_ini, HGLCORE, "BGM", 1));
    set_check(dialog, IDC_EFFECTS, ini_get_bool(&tod_ini, HGLCORE, "SE", 1));
    set_check(dialog, IDC_VOICE, ini_get_bool(&tod_ini, HGLCORE, "Voice", 1));
    set_check(dialog, IDC_BACKGROUND, ini_get_bool(&tod_ini, HGLCORE, "Active", 0));
    set_check(dialog, IDC_SHOW_VERSION, ini_get_bool(&tod_ini, HGLCORE, "DispVersion", 0));
    char limiter[16] = "timer";
    ini_get(&todmod_ini, "timing", "limiter", limiter, sizeof limiter);
    set_check(dialog, IDC_LOW_CPU_LIMITER, _stricmp(limiter, "spin") != 0);
    set_check(dialog, IDC_FRAME_INTERPOLATION, ini_get_int(&todmod_ini, "timing", "frame_interpolation", 0));

    fill_pack_lists(dialog);
    if (SendDlgItemMessageA(dialog, IDC_PACKS_ACTIVE, LB_SETCURSEL, 0, 0) != LB_ERR) {
        show_pack_description(dialog, IDC_PACKS_ACTIVE);
    }

    char face[LF_FACESIZE] = "Arial";
    ini_get(&todmod_ini, "text", "font", face, sizeof face);
    fill_font_list(dialog);
    select_font(dialog, face);
    SetDlgItemInt(dialog, IDC_FONT_SIZE, ini_get_int(&todmod_ini, "text", "cap_height", NATIVE_CAP_HEIGHT), FALSE);
    set_check(dialog, IDC_FONT_BOLD, ini_get_int(&todmod_ini, "text", "weight", FW_NORMAL) >= FW_SEMIBOLD);
    set_check(dialog, IDC_HALF_WIDTH, ini_get_int(&todmod_ini, "text", "half_width", 1));
    char texture_packs[MAX_PACK_LIST] = "";
    ini_get(&todmod_ini, "textures", "packs", texture_packs, sizeof texture_packs);
    SetDlgItemTextA(dialog, IDC_TEXTURE_PACKS, texture_packs);
    char music_packs[MAX_PACK_LIST] = "";
    ini_get(&todmod_ini, "music", "packs", music_packs, sizeof music_packs);
    SetDlgItemTextA(dialog, IDC_MUSIC_PACKS, music_packs);
    char voice_packs[MAX_PACK_LIST] = "";
    ini_get(&todmod_ini, "sound", "voice_packs", voice_packs, sizeof voice_packs);
    SetDlgItemTextA(dialog, IDC_VOICE_PACKS, voice_packs);
    show_volume(dialog, IDC_MUSIC_VOLUME, ini_get_int(&todmod_ini, "sound", "music_volume", 100));
    show_volume(dialog, IDC_EFFECTS_VOLUME, ini_get_int(&todmod_ini, "sound", "effects_volume", 100));
    show_volume(dialog, IDC_VOICE_VOLUME, ini_get_int(&todmod_ini, "sound", "voice_volume", 100));
    set_check(dialog, IDC_SHOW_SCORE, ini_get_int(&todmod_ini, "display", "show_score", 0));
    set_check(dialog, IDC_SHOW_TYPING, ini_get_int(&todmod_ini, "display", "show_typing", 0));
    set_check(dialog, IDC_SPEAK_WORDS, ini_get_int(&todmod_ini, "fun", "speak_words", 0));
    set_check(dialog, IDC_TYPEWRITER, ini_get_int(&todmod_ini, "fun", "typewriter_gun", 0));
    set_check(dialog, IDC_PHOTO_MODE, ini_get_int(&todmod_ini, "fun", "photo_mode", 0));
    set_check(dialog, IDC_MUTE_BACKGROUND, ini_get_int(&todmod_ini, "sound", "mute_in_background", 0));
    set_check(dialog, IDC_HIDE_READING, ini_get_int(&todmod_ini, "typing", "hide_reading", 0));
    {
        int fov = ini_get_int(&todmod_ini, "display", "fov", 0);
        SendDlgItemMessageA(dialog, IDC_FOV, TBM_SETRANGE, FALSE, MAKELPARAM(FOV_MIN, FOV_MAX));
        SendDlgItemMessageA(dialog, IDC_FOV, TBM_SETPOS, TRUE, fov >= FOV_MIN && fov <= FOV_MAX ? fov : GAME_FOV);
        show_fov_text(dialog);
    }
    set_check(dialog, IDC_CASE_SENSITIVE, ini_get_int(&todmod_ini, "typing", "case_sensitive", 0));
    set_check(dialog, IDC_CASE_SCORING, ini_get_int(&todmod_ini, "typing", "case_scoring", 1));
    update_case_scoring_field(dialog);
    char language[16] = "japanese";
    ini_get(&todmod_ini, "text", "language", language, sizeof language);
    set_check(dialog, IDC_ENGLISH_MODE, _stricmp(language, "english") == 0);
    set_check(dialog, IDC_MIX_PACKS, ini_get_int(&todmod_ini, "dictionary", "mix", 0));
    set_check(dialog, IDC_IN_ORDER, ini_get_int(&todmod_ini, "dictionary", "in_order", 0));
    char model_packs[MAX_PACK_LIST] = "";
    ini_get(&todmod_ini, "models", "packs", model_packs, sizeof model_packs);
    list_model_pack_folders(dialog);
    SetDlgItemTextA(dialog, IDC_MODEL_PACKS, model_packs);
    set_check(dialog, IDC_DUMP_MODELS, ini_get_int(&todmod_ini, "models", "dump", 0));
    set_check(dialog, IDC_LOG_TEXTURES, ini_get_int(&todmod_ini, "debug", "log_textures", 0));
    set_check(dialog, IDC_LOG_MODELS, ini_get_int(&todmod_ini, "debug", "log_models", 0));
    set_check(dialog, IDC_SKIP_CUTSCENES, ini_get_int(&todmod_ini, "gameplay", "skip_cutscenes", 0));
    set_check(dialog, IDC_TYPE_SPACES, ini_get_int(&todmod_ini, "typing", "type_spaces", 1));
    set_check(dialog, IDC_BACKSPACE_CANCELS, ini_get_int(&todmod_ini, "input", "backspace_cancels", 1));
    set_check(dialog, IDC_BIG_HEADS, ini_get_int(&todmod_ini, "cheats", "big_heads", 0));
    set_check(dialog, IDC_INFINITE_LIVES, ini_get_int(&todmod_ini, "cheats", "infinite_lives", 0));
    char suit[16] = "none";
    ini_get(&todmod_ini, "cheats", "suit", suit, sizeof suit);
    int chosen_suit = 0;
    for (int i = 0; i < SUIT_COUNT; i++) {
        add_combo_item(dialog, IDC_SUIT, suits[i].label, i);
        if (_stricmp(suit, suits[i].setting) == 0) chosen_suit = i;
    }
    select_combo_data(dialog, IDC_SUIT, chosen_suit);
    add_combo_item(dialog, IDC_PLAYER1_CHARACTER, "Normal", NORMAL_CHARACTER);
    add_combo_item(dialog, IDC_PLAYER2_CHARACTER, "Normal", NORMAL_CHARACTER);
    for (int i = 0; i < CHARACTER_COUNT; i++) {
        char label[96];
        snprintf(label, sizeof label, "%d: %s", i, character_names[i]);
        add_combo_item(dialog, IDC_PLAYER1_CHARACTER, label, i);
        add_combo_item(dialog, IDC_PLAYER2_CHARACTER, label, i);
    }
    select_combo_data(dialog, IDC_PLAYER1_CHARACTER,
                      ini_get_int(&todmod_ini, "cheats", "player1_character", NORMAL_CHARACTER));
    select_combo_data(dialog, IDC_PLAYER2_CHARACTER,
                      ini_get_int(&todmod_ini, "cheats", "player2_character", NORMAL_CHARACTER));
    char layout[16] = "windows";
    ini_get(&todmod_ini, "input", "keyboard_layout", layout, sizeof layout);
    int chosen_layout = 0;
    for (int i = 0; i < KEYBOARD_LAYOUT_COUNT; i++) {
        add_combo_item(dialog, IDC_KEYBOARD_LAYOUT, keyboard_layouts[i].label, i);
        if (_stricmp(layout, keyboard_layouts[i].setting) == 0) chosen_layout = i;
    }
    select_combo_data(dialog, IDC_KEYBOARD_LAYOUT, chosen_layout);
}

static int save_settings(HWND dialog) {
    resolution r;
    if (!chosen_resolution(dialog, &r)) {
        MessageBoxA(dialog, "Please enter a size between 320 x 160 and 7680 x 7680.", "Resolution", MB_ICONWARNING);
        return 0;
    }
    int font_size = GetDlgItemInt(dialog, IDC_FONT_SIZE, NULL, FALSE);
    if (font_size < MIN_CAP_HEIGHT || font_size > MAX_CAP_HEIGHT) {
        MessageBoxA(dialog, "Please enter a letter height between 10 and 24.", "Letter height", MB_ICONWARNING);
        return 0;
    }

    int table_index = original_resolution_index(r);
    if (table_index >= 0) ini_set_int(&tod_ini, HGLCORE, "Resolution", table_index);
    ini_set_bool(&tod_ini, HGLCORE, "Window", is_checked(dialog, IDC_WINDOWED));
    ini_set_int(&todmod_ini, "display", "borderless", is_checked(dialog, IDC_BORDERLESS));
    ini_set(&todmod_ini, "display", "blood", blood_colours[selected_combo_data(dialog, IDC_RED_BLOOD)]);
    ini_set_int(&tod_ini, HGLCORE, "Bpp", (int)selected_combo_data(dialog, IDC_BPP));
    ini_set_int(&tod_ini, HGLCORE, "Filter", (int)selected_combo_data(dialog, IDC_FILTER));
    ini_set_int(&tod_ini, HGLCORE, "GameSpec", (int)selected_combo_data(dialog, IDC_GAMESPEC));
    ini_set_bool(&tod_ini, HGLCORE, "Fog", is_checked(dialog, IDC_FOG));
    ini_set_bool(&tod_ini, HGLCORE, "Transparency", is_checked(dialog, IDC_TRANSPARENCY));
    ini_set_bool(&tod_ini, HGLCORE, "SmallTexture", is_checked(dialog, IDC_SMALL_TEXTURES));
    ini_set_bool(&tod_ini, HGLCORE, "Sound", is_checked(dialog, IDC_SOUND));
    ini_set_bool(&tod_ini, HGLCORE, "BGM", is_checked(dialog, IDC_MUSIC));
    ini_set_bool(&tod_ini, HGLCORE, "SE", is_checked(dialog, IDC_EFFECTS));
    ini_set_bool(&tod_ini, HGLCORE, "Voice", is_checked(dialog, IDC_VOICE));
    ini_set_bool(&tod_ini, HGLCORE, "Active", is_checked(dialog, IDC_BACKGROUND));
    ini_set_bool(&tod_ini, HGLCORE, "DispVersion", is_checked(dialog, IDC_SHOW_VERSION));

    ini_set_int(&todmod_ini, "display", "width", r.width);
    ini_set_int(&todmod_ini, "display", "height", r.height);
    ini_set_int(&todmod_ini, "display", "widescreen", is_checked(dialog, IDC_WIDESCREEN));
    ini_set(&todmod_ini, "timing", "limiter", is_checked(dialog, IDC_LOW_CPU_LIMITER) ? "timer" : "spin");
    ini_set_int(&todmod_ini, "timing", "frame_interpolation", is_checked(dialog, IDC_FRAME_INTERPOLATION));
    char packs[MAX_PACK_LIST];
    active_pack_list(dialog, packs, sizeof packs);
    ini_set(&todmod_ini, "dictionary", "packs", packs);
    wchar_t face_wide[LF_FACESIZE] = L"Arial";
    GetDlgItemTextW(dialog, IDC_FONT, face_wide, LF_FACESIZE);
    char face[LF_FACESIZE * 2];
    WideCharToMultiByte(CP_ACP, 0, face_wide, -1, face, sizeof face, NULL, NULL);
    ini_set(&todmod_ini, "text", "font", face);
    ini_set_int(&todmod_ini, "text", "weight", is_checked(dialog, IDC_FONT_BOLD) ? FW_BOLD : FW_NORMAL);
    ini_set_int(&todmod_ini, "text", "cap_height", font_size);
    ini_set_int(&todmod_ini, "text", "half_width", is_checked(dialog, IDC_HALF_WIDTH));
    char texture_packs[MAX_PACK_LIST];
    GetDlgItemTextA(dialog, IDC_TEXTURE_PACKS, texture_packs, sizeof texture_packs);
    ini_set(&todmod_ini, "textures", "packs", texture_packs);
    char music_packs[MAX_PACK_LIST];
    GetDlgItemTextA(dialog, IDC_MUSIC_PACKS, music_packs, sizeof music_packs);
    ini_set(&todmod_ini, "music", "packs", music_packs);
    char voice_packs[MAX_PACK_LIST];
    GetDlgItemTextA(dialog, IDC_VOICE_PACKS, voice_packs, sizeof voice_packs);
    ini_set(&todmod_ini, "sound", "voice_packs", voice_packs);
    ini_set_int(&todmod_ini, "sound", "music_volume", volume_position(dialog, IDC_MUSIC_VOLUME));
    ini_set_int(&todmod_ini, "sound", "effects_volume", volume_position(dialog, IDC_EFFECTS_VOLUME));
    ini_set_int(&todmod_ini, "sound", "voice_volume", volume_position(dialog, IDC_VOICE_VOLUME));
    ini_set_int(&todmod_ini, "display", "show_score", is_checked(dialog, IDC_SHOW_SCORE));
    ini_set_int(&todmod_ini, "display", "show_typing", is_checked(dialog, IDC_SHOW_TYPING));
    ini_set_int(&todmod_ini, "fun", "speak_words", is_checked(dialog, IDC_SPEAK_WORDS));
    ini_set_int(&todmod_ini, "fun", "typewriter_gun", is_checked(dialog, IDC_TYPEWRITER));
    ini_set_int(&todmod_ini, "fun", "photo_mode", is_checked(dialog, IDC_PHOTO_MODE));
    ini_set_int(&todmod_ini, "sound", "mute_in_background", is_checked(dialog, IDC_MUTE_BACKGROUND));
    ini_set_int(&todmod_ini, "typing", "hide_reading", is_checked(dialog, IDC_HIDE_READING));
    {
        int fov = (int)SendDlgItemMessageA(dialog, IDC_FOV, TBM_GETPOS, 0, 0);
        ini_set_int(&todmod_ini, "display", "fov", fov == GAME_FOV ? 0 : fov);
    }
    {
        int size, position;
        subtitle_page_layout(&size, &position);
        ini_set_int(&todmod_ini, "text", "subtitle_size", size);
        ini_set_int(&todmod_ini, "text", "subtitle_position", position);
    }
    ini_set_int(&todmod_ini, "typing", "case_sensitive", is_checked(dialog, IDC_CASE_SENSITIVE));
    ini_set_int(&todmod_ini, "typing", "case_scoring", is_checked(dialog, IDC_CASE_SCORING));
    ini_set(&todmod_ini, "text", "language", is_checked(dialog, IDC_ENGLISH_MODE) ? "english" : "japanese");
    ini_set_int(&todmod_ini, "dictionary", "mix", is_checked(dialog, IDC_MIX_PACKS));
    ini_set_int(&todmod_ini, "dictionary", "in_order", is_checked(dialog, IDC_IN_ORDER));
    char model_packs[MAX_PACK_LIST];
    GetDlgItemTextA(dialog, IDC_MODEL_PACKS, model_packs, sizeof model_packs);
    ini_set(&todmod_ini, "models", "packs", model_packs);
    ini_set_int(&todmod_ini, "models", "dump", is_checked(dialog, IDC_DUMP_MODELS));
    ini_set_int(&todmod_ini, "debug", "log_textures", is_checked(dialog, IDC_LOG_TEXTURES));
    ini_set_int(&todmod_ini, "debug", "log_models", is_checked(dialog, IDC_LOG_MODELS));
    ini_set_int(&todmod_ini, "gameplay", "skip_cutscenes", is_checked(dialog, IDC_SKIP_CUTSCENES));
    ini_set_int(&todmod_ini, "typing", "type_spaces", is_checked(dialog, IDC_TYPE_SPACES));
    ini_set_int(&todmod_ini, "input", "backspace_cancels", is_checked(dialog, IDC_BACKSPACE_CANCELS));
    ini_set_int(&todmod_ini, "cheats", "big_heads", is_checked(dialog, IDC_BIG_HEADS));
    ini_set_int(&todmod_ini, "cheats", "infinite_lives", is_checked(dialog, IDC_INFINITE_LIVES));
    ini_set(&todmod_ini, "cheats", "suit", suits[selected_combo_data(dialog, IDC_SUIT)].setting);
    char subtitle_file[MAX_PATH];
    subtitle_page_chosen_file(subtitle_file, sizeof subtitle_file);
    ini_set(&todmod_ini, "text", "subtitles", subtitle_file);
    char subtitle_font[LF_FACESIZE];
    int subtitle_font_everywhere;
    subtitle_page_font(subtitle_font, sizeof subtitle_font, &subtitle_font_everywhere);
    if (subtitle_font[0]) ini_set(&todmod_ini, "text", "subtitle_font", subtitle_font);
    ini_set_int(&todmod_ini, "text", "subtitle_windows_font", subtitle_font_everywhere);
    ini_set_int(&todmod_ini, "text", "subtitles_cover_voice", subtitle_page_cover_voice());
    randomizer_settings random_settings;
    randomizer_page_settings(&random_settings);
    online_page_save(&todmod_ini);
    mutators_page_save(&todmod_ini);
    replays_page_save(&todmod_ini);
    ini_set_int(&todmod_ini, "randomizer", "enabled", random_settings.enabled);
    ini_set_int(&todmod_ini, "randomizer", "enemies", random_settings.enemies);
    ini_set_int(&todmod_ini, "randomizer", "people", random_settings.people);
    ini_set_int(&todmod_ini, "randomizer", "mix", random_settings.mix);
    ini_set_int(&todmod_ini, "randomizer", "voices", random_settings.voices);
    ini_set_int(&todmod_ini, "randomizer", "items", random_settings.items);
    ini_set_int(&todmod_ini, "randomizer", "weapons", random_settings.weapons);
    ini_set_int(&todmod_ini, "randomizer", "seed", (int)random_settings.seed);
    ini_set_int(&todmod_ini, "randomizer", "human_zombies", random_settings.human_zombies);
    ini_set_int(&todmod_ini, "randomizer", "enemy_min", random_settings.enemy_min);
    ini_set_int(&todmod_ini, "randomizer", "enemy_max", random_settings.enemy_max);
    ini_set_int(&todmod_ini, "cheats", "player1_character", (int)selected_combo_data(dialog, IDC_PLAYER1_CHARACTER));
    ini_set_int(&todmod_ini, "cheats", "player2_character", (int)selected_combo_data(dialog, IDC_PLAYER2_CHARACTER));
    ini_set(&todmod_ini, "input", "keyboard_layout",
            keyboard_layouts[selected_combo_data(dialog, IDC_KEYBOARD_LAYOUT)].setting);

    if (!ini_save(&tod_ini, tod_ini_path) || !ini_save(&todmod_ini, todmod_ini_path)) {
        MessageBoxA(dialog, "Could not write the settings files. Is the game folder read-only?", "Save failed",
                    MB_ICONERROR);
        return 0;
    }
    set_status(dialog, "Settings saved.");
    return 1;
}

static void start_game(HWND dialog) {
    char exe[MAX_PATH];
    join_path(exe, game_folder, "Tod.exe");
    STARTUPINFOA startup = {sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, 0, NULL, game_folder, &startup, &process)) {
        MessageBoxA(dialog, "Could not start Tod.exe.", "Play", MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    EndDialog(dialog, IDOK);
}

static void open_folder(const char *folder) {
    CreateDirectoryA(folder, NULL);
    ShellExecuteA(NULL, "open", folder, NULL, NULL, SW_SHOWNORMAL);
}

static void open_dict_folder(void) {
    CreateDirectoryA(dict_folder, NULL);
    ShellExecuteA(NULL, "open", dict_folder, NULL, NULL, SW_SHOWNORMAL);
}

static void pack_name_from_path(const char *path, char *name, size_t name_size) {
    const char *file = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
    size_t length = 0;
    for (; *file && *file != '.' && length + 1 < name_size; file++) {
        char c = *file;
        int allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        name[length++] = allowed ? c : '_';
    }
    name[length] = '\0';
    if (!length) snprintf(name, name_size, "my_words");
}

static void put_pack_at_top(HWND dialog, const char *name) {
    HWND available = GetDlgItem(dialog, IDC_PACKS_AVAILABLE), active = GetDlgItem(dialog, IDC_PACKS_ACTIVE);
    LRESULT index = SendMessageA(available, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)name);
    if (index != LB_ERR) SendMessageA(available, LB_DELETESTRING, index, 0);
    index = SendMessageA(active, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)name);
    if (index != LB_ERR) SendMessageA(active, LB_DELETESTRING, index, 0);
    SendMessageA(active, LB_INSERTSTRING, 0, (LPARAM)name);
    SendMessageA(active, LB_SETCURSEL, 0, 0);
}

static void create_pack_from_word_list(HWND dialog) {
    char word_list[MAX_PATH] = "";
    OPENFILENAMEA dialog_options = {sizeof dialog_options};
    dialog_options.hwndOwner = dialog;
    dialog_options.lpstrFilter = "Word lists (*.txt)\0*.txt\0All files\0*.*\0";
    dialog_options.lpstrFile = word_list;
    dialog_options.nMaxFile = sizeof word_list;
    dialog_options.lpstrTitle = "Choose a UTF-8 word list (one word or phrase per line)";
    dialog_options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&dialog_options)) return;

    char name[64], pack_folder[MAX_PATH], question[256];
    pack_name_from_path(word_list, name, sizeof name);
    CreateDirectoryA(dict_folder, NULL);
    snprintf(pack_folder, sizeof pack_folder, "%s\\%s", dict_folder, name);
    if (GetFileAttributesA(pack_folder) != INVALID_FILE_ATTRIBUTES) {
        snprintf(question, sizeof question, "A pack called \"%s\" already exists. Replace its word lists?", name);
        if (MessageBoxA(dialog, question, "New pack", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    word_pack_half_width = is_checked(dialog, IDC_HALF_WIDTH);
    HCURSOR previous_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    pack_build_report report;
    char error[512];
    int built = build_pack_from_word_list(word_list, pack_folder, GetTickCount(), &report, error, sizeof error);
    SetCursor(previous_cursor);
    if (!built) {
        MessageBoxA(dialog, error, "New pack", MB_ICONWARNING);
        return;
    }
    put_pack_at_top(dialog, name);
    char summary[768];
    snprintf(summary, sizeof summary,
             "Pack \"%s\" created from %d words.\n\n"
             "Used: %d\nSkipped, nothing to type: %d\nSkipped, longer than 24 characters: %d\nDuplicates: %d\n\n"
             "%d word lists filled (%d slots). %d quiz and special lists keep the game's own words.\n\n"
             "The pack is now at the top of \"In use\". Click Save to keep this choice.",
             name, report.words_read, report.words_usable, report.untypable, report.too_long, report.duplicates,
             report.lists_written, report.slots_filled, report.lists_kept);
    MessageBoxA(dialog, summary, "New pack", MB_ICONINFORMATION);
    set_status(dialog, "New pack created - click Save.");
}

static void show_pack_coverage(HWND dialog) {
    HWND active = GetDlgItem(dialog, IDC_PACKS_ACTIVE);
    int count = (int)SendMessageA(active, LB_GETCOUNT, 0, 0);
    char (*names)[MAX_PATH] = calloc(count ? count : 1, sizeof *names);
    const char **packs = calloc(count ? count : 1, sizeof *packs);
    for (int i = 0; i < count; i++) {
        SendMessageA(active, LB_GETTEXT, i, (LPARAM)names[i]);
        packs[i] = names[i];
    }
    HCURSOR previous_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    char report[4096];
    int complete = pack_coverage_report(dict_folder, packs, count, report, sizeof report);
    SetCursor(previous_cursor);
    MessageBoxA(dialog, report, "Word pack coverage", complete ? MB_ICONINFORMATION : MB_ICONWARNING);
    free(packs);
    free(names);
}

#define PLAYER1_KIND 57

typedef struct {
    char model[MAX_PATH];
    char pack[64];
    int kind;
    int use_pack;
    int play_as;
} model_import;

static int has_extension(const char *path, const char *extension) {
    size_t length = strlen(path), extension_length = strlen(extension);
    return length > extension_length && _stricmp(path + length - extension_length, extension) == 0;
}

static INT_PTR CALLBACK import_model_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    model_import *request = (model_import *)GetWindowLongPtrA(dialog, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtrA(dialog, DWLP_USER, lparam);
        for (int kind = 0; kind < CHARACTER_COUNT; kind++) {
            if (strstr(character_names[kind], "may be invisible")) continue;
            char label[160];
            snprintf(label, sizeof label, "%d: %s", kind, character_names[kind]);
            add_combo_item(dialog, IDC_IMPORT_KIND, label, kind);
        }
        select_combo_data(dialog, IDC_IMPORT_KIND, PLAYER1_KIND);
        CheckDlgButton(dialog, IDC_IMPORT_USE_PACK, BST_CHECKED);
        PostMessageA(dialog, WM_COMMAND, IDC_IMPORT_BROWSE, 0);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_IMPORT_BROWSE: {
            char path[MAX_PATH] = "";
            OPENFILENAMEA options = {sizeof options};
            options.hwndOwner = dialog;
            options.lpstrFilter = "3D models (*.obj, *.gltf, *.glb)\0*.obj;*.gltf;*.glb\0All files\0*.*\0";
            options.lpstrFile = path;
            options.nMaxFile = sizeof path;
            options.lpstrTitle = "Choose a character model";
            options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (!GetOpenFileNameA(&options)) return TRUE;
            SetDlgItemTextA(dialog, IDC_IMPORT_FILE, path);
            char name[64];
            pack_name_from_path(path, name, sizeof name);
            SetDlgItemTextA(dialog, IDC_IMPORT_PACK, name);
            return TRUE;
        }
        case IDOK:
            GetDlgItemTextA(dialog, IDC_IMPORT_FILE, request->model, sizeof request->model);
            GetDlgItemTextA(dialog, IDC_IMPORT_PACK, request->pack, sizeof request->pack);
            if (!request->model[0] || !request->pack[0] || strpbrk(request->pack, "\\/:*?\"<>|, ")) {
                MessageBoxA(dialog, "Choose a model and a pack name (letters, digits, - and _).", "Import", MB_ICONWARNING);
                return TRUE;
            }
            request->kind = (int)selected_combo_data(dialog, IDC_IMPORT_KIND);
            request->use_pack = IsDlgButtonChecked(dialog, IDC_IMPORT_USE_PACK) == BST_CHECKED;
            request->play_as = IsDlgButtonChecked(dialog, IDC_IMPORT_PLAY_AS) == BST_CHECKED;
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int find_converter(const char *script, char *out) {
    char exe_folder[MAX_PATH];
    GetModuleFileNameA(NULL, exe_folder, MAX_PATH);
    char *slash = strrchr(exe_folder, '\\');
    if (slash) *slash = '\0';
    const char *patterns[] = {"%s\\tools\\%s", "%s\\..\\tools\\%s"};
    const char *bases[] = {game_folder, exe_folder};
    for (int b = 0; b < 2; b++) {
        for (int p = 0; p < 2; p++) {
            snprintf(out, MAX_PATH, patterns[p], bases[b], script);
            if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 1;
        }
    }
    return 0;
}

static int run_converter(const char *command_line, const char *output_path, DWORD *exit_code) {
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE output = CreateFileA(output_path, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE) return 0;
    STARTUPINFOA startup = {sizeof startup};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process;
    char command[4 * MAX_PATH];
    snprintf(command, sizeof command, "%s", command_line);
    BOOL started = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, game_folder, &startup, &process);
    CloseHandle(output);
    if (!started) return 0;
    while (MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
        MSG pending;
        while (PeekMessageA(&pending, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageA(&pending);
    }
    GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}

static void read_tail(const char *path, char *out, size_t size) {
    out[0] = '\0';
    FILE *file = fopen(path, "rb");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    long length = ftell(file), start = length > (long)size - 1 ? length - (long)size + 1 : 0;
    fseek(file, start, SEEK_SET);
    size_t read = fread(out, 1, size - 1, file);
    out[read] = '\0';
    fclose(file);
}

static void put_model_pack_first(HWND dialog, const char *pack) {
    char packs[MAX_PACK_LIST], updated[MAX_PACK_LIST];
    GetDlgItemTextA(dialog, IDC_MODEL_PACKS, packs, sizeof packs);
    snprintf(updated, sizeof updated, "%s", pack);
    for (char *item = strtok(packs, ","); item; item = strtok(NULL, ",")) {
        while (*item == ' ') item++;
        if (!*item || _stricmp(item, pack) == 0) continue;
        strncat(updated, ",", sizeof updated - strlen(updated) - 1);
        strncat(updated, item, sizeof updated - strlen(updated) - 1);
    }
    SetDlgItemTextA(dialog, IDC_MODEL_PACKS, updated);
}

static void import_character_model(HWND dialog) {
    model_import request = {0};
    if (DialogBoxParamA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDD_IMPORT_MODEL), dialog, import_model_dialog,
                        (LPARAM)&request) != IDOK)
        return;
    int gltf = has_extension(request.model, ".gltf") || has_extension(request.model, ".glb");
    char script[MAX_PATH];
    if (!find_converter(gltf ? "gltf_to_character.py" : "obj_to_character.py", script)) {
        MessageBoxA(dialog, "The converter scripts were not found in the game's tools folder.", "Import", MB_ICONWARNING);
        return;
    }
    char output_path[MAX_PATH], arguments[3 * MAX_PATH + 128], command[4 * MAX_PATH];
    join_path(output_path, game_folder, "todmod_import.log");
    snprintf(arguments, sizeof arguments, "\"%s\" \"%s\" \"%s\" %s %d", script, request.model, game_folder, request.pack,
             request.kind);
    HCURSOR previous_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    set_status(dialog, "Converting the model...");
    DWORD exit_code = 1;
    const char *launchers[] = {"python", "py -3"};
    int started = 0;
    for (int i = 0; i < 2 && !started; i++) {
        snprintf(command, sizeof command, "%s %s", launchers[i], arguments);
        started = run_converter(command, output_path, &exit_code);
    }
    SetCursor(previous_cursor);
    char output[1500];
    read_tail(output_path, output, sizeof output);
    if (!started || exit_code != 0) {
        char message[2048];
        snprintf(message, sizeof message, "%s\n\n%s",
                 started ? "The conversion failed. Last lines of todmod_import.log:"
                         : "Python was not found. Install Python 3, then: pip install pillow numpy",
                 output);
        MessageBoxA(dialog, message, "Import", MB_ICONWARNING);
        set_status(dialog, "Import failed.");
        return;
    }
    if (request.use_pack) put_model_pack_first(dialog, request.pack);
    if (request.play_as && request.kind != PLAYER1_KIND) select_combo_data(dialog, IDC_PLAYER1_CHARACTER, request.kind);
    char message[2048];
    snprintf(message, sizeof message, "Pack \"%s\" created in models\\%s.\n\n%s\n\nClick Save to keep the pack choice.",
             request.pack, request.pack, output);
    MessageBoxA(dialog, message, "Import", MB_ICONINFORMATION);
    set_status(dialog, "Model imported - click Save.");
}

static HWND main_dialog;

static void practice_pack_built(const char *pack_name) {
    put_pack_at_top(main_dialog, pack_name);
    set_status(main_dialog, "Practice pack made - click Save.");
}

static INT_PTR CALLBACK config_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (character_page_message(dialog, message, wparam, lparam)) return TRUE;
    switch (message) {
    case WM_INITDIALOG:
        show_settings(dialog);
        {
            char subtitle_file[MAX_PATH] = "";
            ini_get(&todmod_ini, "text", "subtitles", subtitle_file, sizeof subtitle_file);
            randomizer_settings random_settings = {
                ini_get_int(&todmod_ini, "randomizer", "enabled", 0), ini_get_int(&todmod_ini, "randomizer", "enemies", 1),
                ini_get_int(&todmod_ini, "randomizer", "people", 1), ini_get_int(&todmod_ini, "randomizer", "mix", 0),
                ini_get_int(&todmod_ini, "randomizer", "voices", 1), ini_get_int(&todmod_ini, "randomizer", "items", 1),
                ini_get_int(&todmod_ini, "randomizer", "weapons", 1), (unsigned)ini_get_int(&todmod_ini, "randomizer", "seed", 0),
                ini_get_int(&todmod_ini, "randomizer", "human_zombies", 0), ini_get_int(&todmod_ini, "randomizer", "enemy_min", 1),
                ini_get_int(&todmod_ini, "randomizer", "enemy_max", 1)};
            character_page_create(dialog, game_folder, subtitle_file, &random_settings, &todmod_ini);
            char subtitle_font[LF_FACESIZE] = "Meiryo";
            ini_get(&todmod_ini, "text", "subtitle_font", subtitle_font, sizeof subtitle_font);
            subtitle_page_set_font(subtitle_font, ini_get_int(&todmod_ini, "text", "subtitle_windows_font", 0));
            subtitle_page_set_cover_voice(ini_get_int(&todmod_ini, "text", "subtitles_cover_voice", 1));
            subtitle_page_set_layout(ini_get_int(&todmod_ini, "text", "subtitle_size", 100),
                                     ini_get_int(&todmod_ini, "text", "subtitle_position", 0));
            main_dialog = dialog;
            stats_page_set_practice_callback(practice_pack_built);
        }
        if (start_on_characters) character_page_open(dialog, start_character);
        if (start_on_subtitles) character_page_open_tab(dialog, 2);
        if (start_on_randomizer) character_page_open_tab(dialog, 3);
        if (start_on_mutators) character_page_open_tab(dialog, 4);
        if (start_on_online) character_page_open_tab(dialog, 5);
        if (start_on_stats) character_page_open_tab(dialog, 6);
        if (start_on_replays) character_page_open_tab(dialog, 7);
        if (start_on_audio) character_page_open_tab(dialog, 8);
        return TRUE;
    case WM_HSCROLL:
        if ((HWND)lparam == GetDlgItem(dialog, IDC_MUSIC_VOLUME)) show_volume_text(dialog, IDC_MUSIC_VOLUME);
        if ((HWND)lparam == GetDlgItem(dialog, IDC_EFFECTS_VOLUME)) show_volume_text(dialog, IDC_EFFECTS_VOLUME);
        if ((HWND)lparam == GetDlgItem(dialog, IDC_VOICE_VOLUME)) show_volume_text(dialog, IDC_VOICE_VOLUME);
        if ((HWND)lparam == GetDlgItem(dialog, IDC_FOV)) show_fov_text(dialog);
        break;
    case WM_DESTROY:
        audio_page_close();
        character_page_destroy();
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_RESOLUTION:
            if (HIWORD(wparam) == CBN_SELCHANGE) update_custom_size_fields(dialog);
            return TRUE;
        case IDC_CASE_SENSITIVE:
            if (HIWORD(wparam) == BN_CLICKED) update_case_scoring_field(dialog);
            return TRUE;
        case IDC_PACK_ADD:
            move_selected_pack(dialog, IDC_PACKS_AVAILABLE, IDC_PACKS_ACTIVE);
            return TRUE;
        case IDC_PACK_REMOVE:
            move_selected_pack(dialog, IDC_PACKS_ACTIVE, IDC_PACKS_AVAILABLE);
            return TRUE;
        case IDC_PACKS_AVAILABLE:
            if (HIWORD(wparam) == LBN_DBLCLK) move_selected_pack(dialog, IDC_PACKS_AVAILABLE, IDC_PACKS_ACTIVE);
            if (HIWORD(wparam) == LBN_SELCHANGE) show_pack_description(dialog, IDC_PACKS_AVAILABLE);
            return TRUE;
        case IDC_PACKS_ACTIVE:
            if (HIWORD(wparam) == LBN_DBLCLK) move_selected_pack(dialog, IDC_PACKS_ACTIVE, IDC_PACKS_AVAILABLE);
            if (HIWORD(wparam) == LBN_SELCHANGE) show_pack_description(dialog, IDC_PACKS_ACTIVE);
            return TRUE;
        case IDC_PACK_UP:
            shift_active_pack(dialog, -1);
            return TRUE;
        case IDC_PACK_DOWN:
            shift_active_pack(dialog, 1);
            return TRUE;
        case IDC_OPEN_DICT:
            open_dict_folder();
            return TRUE;
        case IDC_OPEN_TEXTURES:
            open_folder(textures_folder);
            return TRUE;
        case IDC_OPEN_MUSIC:
            open_folder(music_folder);
            return TRUE;
        case IDC_OPEN_MODELS:
            open_folder(models_folder);
            return TRUE;
        case IDC_OPEN_VOICES:
            open_folder(voices_folder);
            return TRUE;
        case IDC_IMPORT_MODEL:
            import_character_model(dialog);
            return TRUE;
        case IDC_PACK_FROM_LIST:
            create_pack_from_word_list(dialog);
            return TRUE;
        case IDC_PACK_COVERAGE:
            show_pack_coverage(dialog);
            return TRUE;
        case IDC_SAVE:
            save_settings(dialog);
            return TRUE;
        case IDC_SAVE_PLAY:
            if (save_settings(dialog)) start_game(dialog);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void read_desktop_resolution(void) {
    DEVMODEA mode = {0};
    mode.dmSize = sizeof mode;
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode)) {
        desktop.width = (int)mode.dmPelsWidth;
        desktop.height = (int)mode.dmPelsHeight;
    } else {
        desktop.width = GetSystemMetrics(SM_CXSCREEN);
        desktop.height = GetSystemMetrics(SM_CYSCREEN);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show) {
    (void)previous;
    (void)show;
    start_on_subtitles = strstr(command_line, "/subtitles") != NULL;
    start_on_randomizer = strstr(command_line, "/randomizer") != NULL;
    start_on_online = strstr(command_line, "/online") != NULL;
    start_on_mutators = strstr(command_line, "/mutators") != NULL;
    start_on_stats = strstr(command_line, "/stats") != NULL;
    start_on_replays = strstr(command_line, "/replays") != NULL;
    start_on_audio = strstr(command_line, "/audio") != NULL;
    const char *characters_option = strstr(command_line, "/characters");
    if (characters_option) {
        start_on_characters = 1;
        start_character = characters_option[11] == ' ' ? atoi(characters_option + 12) : NORMAL_CHARACTER;
    }
    INITCOMMONCONTROLSEX controls = {sizeof controls, ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    resolve_paths();
    read_desktop_resolution();
    ini_load(&tod_ini, tod_ini_path);
    load_todmod_ini();
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), NULL, config_dialog, 0);
    ini_free(&tod_ini);
    ini_free(&todmod_ini);
    return 0;
}
