#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_page.h"
#include "subtitle_text.h"

#define ROW_HEIGHT 22
#define BUTTON_HEIGHT 26
#define BUTTON_WIDTH 90
#define GAP 8
#define MAX_ITEMS 4096
#define SOUND_RECORDS_VA 0x0051cfd0
#define SOUND_RECORD_SIZE 16
#define SOUND_RECORD_SOUND 12
#define SOUND_RECORD_COUNT 402
#define TEXT_STEPS_VA 0x00524a28
#define TEXT_STEPS 4
#define VOICE_NAMES_VA 0x0053b1a4
#define VOICE_NAME_SIZE 0x24
#define VOICE_KIND 2
#define ALIAS "todmod_audio"
#define TIMER_ID 2701

enum {
    IDC_AUDIO_MUSIC = 2700,
    IDC_AUDIO_KIND,
    IDC_AUDIO_SOUNDS,
    IDC_AUDIO_PLAY,
    IDC_AUDIO_PAUSE,
    IDC_AUDIO_STOP,
    IDC_AUDIO_STATUS,
    IDC_AUDIO_LABEL,
};

typedef struct {
    char path[MAX_PATH];
    wchar_t label[160];
} audio_item;

static const struct {
    const char *file;
    const wchar_t *name;
} music_names[] = {
    {"ADV_AR", L"Attract demo"},
    {"BOSS", L"Boss"},
    {"BOSS_MOD", L"Boss Mode"},
    {"CLR", L"Chapter clear"},
    {"CLR2", L"Chapter clear (alternate)"},
    {"ENDL", L"Ending (long)"},
    {"ENDS", L"Ending (short)"},
    {"HOD1_ADV", L"The House of the Dead attract demo"},
    {"ITEM_SELECT", L"Item select"},
    {"NAME", L"Name entry"},
    {"OVR_AR", L"Game over"},
    {"RANK_MOD", L"Ranking Mode"},
    {"ST1", L"Chapter 1"},
    {"ST2", L"Chapter 2"},
    {"ST3", L"Chapter 3"},
    {"ST4", L"Chapter 4"},
    {"ST5", L"Chapter 5"},
    {"ST5BOS1", L"Chapter 5 boss, part 1"},
    {"ST5BOS2", L"Chapter 5 boss, part 2"},
    {"ST6", L"Chapter 6"},
    {"ST6BOS1", L"Final boss, part 1"},
    {"ST6BOS2", L"Final boss, part 2"},
    {"TRA_MOD", L"Training Mode"},
};

static HWND controls[16];
static int control_count;
static HWND music_list, kind_box, sound_list, status_text, pause_button;
static char game_folder_path[MAX_PATH];
static audio_item *music, *voices, *effects;
static int music_count, voice_count, effect_count, loaded;
#define SUBTITLE_CHARS 256
static wchar_t subtitle_text[SUBTITLE_COUNT][SUBTITLE_CHARS];
static int playing, paused;

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                        int width, int height, int id) {
    HWND control = CreateWindowExW(lstrcmpW(class_name, WC_LISTBOXW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                   WS_CHILD | style, x, y, width, height, dialog, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static int compare_items(const void *a, const void *b) {
    return lstrcmpiW(((const audio_item *)a)->label, ((const audio_item *)b)->label);
}

static void add_files(const char *folder, const char *relative, const char *pattern, audio_item *items, int *count) {
    char search[MAX_PATH];
    snprintf(search, sizeof search, "%s%s*", folder, relative);
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(search, &found);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if (found.cFileName[0] == '.') continue;
        char child[MAX_PATH];
        snprintf(child, sizeof child, "%s%s", relative, found.cFileName);
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            strncat(child, "\\", sizeof child - strlen(child) - 1);
            add_files(folder, child, pattern, items, count);
            continue;
        }
        const char *dot = strrchr(found.cFileName, '.');
        if (!dot || _stricmp(dot, pattern) != 0 || *count >= MAX_ITEMS) continue;
        audio_item *item = &items[(*count)++];
        snprintf(item->path, sizeof item->path, "%s%s", folder, child);
        MultiByteToWideChar(CP_ACP, 0, child, -1, item->label, 160);
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
}

static long file_offset_of(FILE *file, DWORD va) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (fseek(file, 0, SEEK_SET) || fread(&dos, sizeof dos, 1, file) != 1 || fseek(file, dos.e_lfanew, SEEK_SET) ||
        fread(&nt, sizeof nt, 1, file) != 1)
        return -1;
    DWORD rva = va - nt.OptionalHeader.ImageBase;
    for (int i = 0; i < nt.FileHeader.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER section;
        if (fread(&section, sizeof section, 1, file) != 1) break;
        if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.SizeOfRawData) {
            return (long)(rva - section.VirtualAddress + section.PointerToRawData);
        }
    }
    return -1;
}

static int read_at(FILE *file, long offset, void *out, size_t size) {
    return offset >= 0 && !fseek(file, offset, SEEK_SET) && fread(out, 1, size, file) == size;
}

static void read_subtitle_text(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sTod.exe", game_folder_path);
    FILE *file = fopen(path, "rb");
    long records = file ? file_offset_of(file, 0x0051eeac) : -1;
    for (int i = 0; i < SUBTITLE_GAME_COUNT && records >= 0; i++) {
        char text[0x39] = "";
        if (!read_at(file, records + i * 0x40, text, 0x38)) break;
        MultiByteToWideChar(932, 0, text, -1, subtitle_text[i], SUBTITLE_CHARS);
    }
    if (file) fclose(file);
    snprintf(path, sizeof path, "%ssubtitles\\english.tsv", game_folder_path);
    file = fopen(path, "rb");
    if (!file) return;
    char line[1024];
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#') continue;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        int index = atoi(line);
        char *text = tab + 1, *end = strpbrk(text, "\t\r\n");
        if (end) *end = '\0';
        if (index >= 0 && index < SUBTITLE_GAME_COUNT && *text)
            MultiByteToWideChar(CP_UTF8, 0, text, -1, subtitle_text[index], SUBTITLE_CHARS);
    }
    fclose(file);
}

static void label_voices_with_subtitles(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sTod.exe", game_folder_path);
    FILE *file = fopen(path, "rb");
    if (!file) return;
    long sounds = file_offset_of(file, SOUND_RECORDS_VA), steps = file_offset_of(file, TEXT_STEPS_VA),
         names = file_offset_of(file, VOICE_NAMES_VA);
    for (int id = 0; id < SOUND_RECORD_COUNT && sounds >= 0 && steps >= 0 && names >= 0; id++) {
        uint32_t sound = 0;
        short record[TEXT_STEPS];
        if (!read_at(file, sounds + id * SOUND_RECORD_SIZE + SOUND_RECORD_SOUND, &sound, 4) || sound >> 28 != VOICE_KIND) continue;
        if (!read_at(file, steps + id * TEXT_STEPS * 2, record, sizeof record)) continue;
        char name[VOICE_NAME_SIZE + 1] = "";
        if (!read_at(file, names + (long)(sound & 0xfff) * VOICE_NAME_SIZE, name, VOICE_NAME_SIZE)) continue;
        wchar_t text[160] = L"";
        for (int step = 0; step < TEXT_STEPS && record[step] >= 0 && record[step] < SUBTITLE_GAME_COUNT; step++) {
            const wchar_t *part = subtitle_text[record[step]];
            if (!part[0]) continue;
            if (text[0]) wcsncat(text, L" ", 159 - wcslen(text));
            wcsncat(text, part, 159 - wcslen(text));
        }
        if (!text[0]) continue;
        for (char *c = name; *c; c++) if (*c == '/') *c = '\\';
        for (int i = 0; i < voice_count; i++) {
            const char *tail = voices[i].path + strlen(voices[i].path) - strlen(name);
            if (tail < voices[i].path || _stricmp(tail, name) != 0) continue;
            wchar_t label[160];
            _snwprintf(label, 160, L"%s   (%hs)", text, name);
            label[159] = 0;
            lstrcpynW(voices[i].label, label, 160);
        }
    }
    fclose(file);
}

static const wchar_t *music_name(const char *path) {
    const char *base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    char stem[64];
    lstrcpynA(stem, base, sizeof stem);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';
    for (size_t i = 0; i < sizeof music_names / sizeof *music_names; i++) {
        if (_stricmp(stem, music_names[i].file) == 0) return music_names[i].name;
    }
    return NULL;
}

static void load_items(void) {
    if (loaded) return;
    loaded = 1;
    music = calloc(MAX_ITEMS, sizeof *music);
    voices = calloc(MAX_ITEMS, sizeof *voices);
    effects = calloc(MAX_ITEMS, sizeof *effects);
    if (!music || !voices || !effects) return;
    char folder[MAX_PATH];
    snprintf(folder, sizeof folder, "%sSOUND\\BGM\\", game_folder_path);
    add_files(folder, "", ".wma", music, &music_count);
    add_files(folder, "", ".wav", music, &music_count);
    for (int i = 0; i < music_count; i++) {
        const wchar_t *name = music_name(music[i].path);
        if (name) {
            wchar_t label[160];
            _snwprintf(label, 160, L"%s   (%s)", name, music[i].label);
            label[159] = 0;
            lstrcpynW(music[i].label, label, 160);
        }
    }
    snprintf(folder, sizeof folder, "%sSOUND\\VOICE\\", game_folder_path);
    add_files(folder, "", ".wav", voices, &voice_count);
    snprintf(folder, sizeof folder, "%sSOUND\\SE\\", game_folder_path);
    add_files(folder, "", ".wav", effects, &effect_count);
    read_subtitle_text();
    label_voices_with_subtitles();
    qsort(music, (size_t)music_count, sizeof *music, compare_items);
}

static void fill(HWND list, const audio_item *items, int count) {
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++) {
        LRESULT row = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)items[i].label);
        SendMessageW(list, LB_SETITEMDATA, (WPARAM)row, (LPARAM)i);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);
}

static void fill_sound_list(void) {
    int dialogue = SendMessageW(kind_box, CB_GETCURSEL, 0, 0) == 0;
    fill(sound_list, dialogue ? voices : effects, dialogue ? voice_count : effect_count);
}

static void set_status(const wchar_t *text) {
    SetWindowTextW(status_text, text);
}

static void stop_playing(void) {
    mciSendStringA("close " ALIAS, NULL, 0, NULL);
    playing = paused = 0;
    SetWindowTextW(pause_button, L"Pause");
}

static void CALLBACK update_position(HWND window, UINT message, UINT_PTR id, DWORD time) {
    (void)window; (void)message; (void)id; (void)time;
    if (!playing) return;
    char mode[32] = "", position[32] = "", length[32] = "";
    mciSendStringA("status " ALIAS " mode", mode, sizeof mode, NULL);
    mciSendStringA("status " ALIAS " position", position, sizeof position, NULL);
    mciSendStringA("status " ALIAS " length", length, sizeof length, NULL);
    if (!strcmp(mode, "stopped") && !paused) {
        stop_playing();
        set_status(L"Finished.");
        return;
    }
    unsigned at = (unsigned)atoi(position) / 1000, total = (unsigned)atoi(length) / 1000;
    wchar_t text[64];
    _snwprintf(text, 64, L"%s %u:%02u / %u:%02u", paused ? L"Paused" : L"Playing", at / 60, at % 60, total / 60, total % 60);
    text[63] = 0;
    set_status(text);
}

static void play_selected(HWND list) {
    LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (row < 0) return;
    int index = (int)SendMessageW(list, LB_GETITEMDATA, (WPARAM)row, 0);
    const audio_item *items = list == music_list ? music : SendMessageW(kind_box, CB_GETCURSEL, 0, 0) == 0 ? voices : effects;
    stop_playing();
    char command[MAX_PATH + 64];
    snprintf(command, sizeof command, "open \"%s\" type mpegvideo alias " ALIAS, items[index].path);
    if (mciSendStringA(command, NULL, 0, NULL) || mciSendStringA("play " ALIAS, NULL, 0, NULL)) {
        stop_playing();
        set_status(L"This file could not be played.");
        return;
    }
    mciSendStringA("set " ALIAS " time format milliseconds", NULL, 0, NULL);
    playing = 1;
    update_position(NULL, 0, 0, 0);
}

void audio_page_create(HWND dialog, RECT area, const char *game_folder) {
    control_count = 0;
    lstrcpynA(game_folder_path, game_folder, MAX_PATH);
    int x = area.left, y = area.top, width = area.right - area.left;
    int half = (width - GAP) / 2;
    add_control(dialog, WC_STATICW, L"Music:", 0, x, y + 4, half, 18, IDC_AUDIO_LABEL);
    kind_box = add_control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, x + half + GAP, y, half, 200, IDC_AUDIO_KIND);
    SendMessageW(kind_box, CB_ADDSTRING, 0, (LPARAM)L"Dialogue (voice lines, with their subtitles)");
    SendMessageW(kind_box, CB_ADDSTRING, 0, (LPARAM)L"Sound effects");
    SendMessageW(kind_box, CB_SETCURSEL, 0, 0);
    y += ROW_HEIGHT + 4;
    int list_height = area.bottom - y - BUTTON_HEIGHT - ROW_HEIGHT - GAP;
    DWORD list_style = WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
    music_list = add_control(dialog, WC_LISTBOXW, L"", list_style, x, y, half, list_height, IDC_AUDIO_MUSIC);
    sound_list = add_control(dialog, WC_LISTBOXW, L"", list_style, x + half + GAP, y, half, list_height, IDC_AUDIO_SOUNDS);
    SendMessageW(music_list, LB_SETHORIZONTALEXTENT, 900, 0);
    SendMessageW(sound_list, LB_SETHORIZONTALEXTENT, 1400, 0);
    y += list_height + GAP;
    add_control(dialog, WC_BUTTONW, L"Play", WS_TABSTOP, x, y, BUTTON_WIDTH, BUTTON_HEIGHT, IDC_AUDIO_PLAY);
    pause_button = add_control(dialog, WC_BUTTONW, L"Pause", WS_TABSTOP, x + BUTTON_WIDTH + GAP, y, BUTTON_WIDTH,
                               BUTTON_HEIGHT, IDC_AUDIO_PAUSE);
    add_control(dialog, WC_BUTTONW, L"Stop", WS_TABSTOP, x + 2 * (BUTTON_WIDTH + GAP), y, BUTTON_WIDTH, BUTTON_HEIGHT,
                IDC_AUDIO_STOP);
    status_text = add_control(dialog, WC_STATICW, L"Double-click or select and press Play. Files play from your game folder.",
                              SS_LEFTNOWORDWRAP, x + 3 * (BUTTON_WIDTH + GAP), y + 5, width - 3 * (BUTTON_WIDTH + GAP), 18,
                              IDC_AUDIO_STATUS);
    SetTimer(dialog, TIMER_ID, 500, update_position);
    audio_page_show(0);
}

void audio_page_show(int visible) {
    if (visible && !loaded) {
        HCURSOR previous = SetCursor(LoadCursor(NULL, IDC_WAIT));
        load_items();
        fill(music_list, music, music_count);
        fill_sound_list();
        SetCursor(previous);
    }
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
    if (!visible && playing) stop_playing();
}

int audio_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)dialog;
    (void)lparam;
    if (message != WM_COMMAND) return 0;
    int id = LOWORD(wparam), code = HIWORD(wparam);
    if (id == IDC_AUDIO_KIND && code == CBN_SELCHANGE) {
        fill_sound_list();
        return 1;
    }
    if ((id == IDC_AUDIO_MUSIC || id == IDC_AUDIO_SOUNDS) && code == LBN_DBLCLK) {
        play_selected(id == IDC_AUDIO_MUSIC ? music_list : sound_list);
        return 1;
    }
    if ((id == IDC_AUDIO_MUSIC || id == IDC_AUDIO_SOUNDS) && code == LBN_SELCHANGE) {
        SendMessageW(id == IDC_AUDIO_MUSIC ? sound_list : music_list, LB_SETCURSEL, (WPARAM)-1, 0);
        return 1;
    }
    if (id == IDC_AUDIO_PLAY) {
        play_selected(SendMessageW(music_list, LB_GETCURSEL, 0, 0) >= 0 ? music_list : sound_list);
        return 1;
    }
    if (id == IDC_AUDIO_PAUSE && playing) {
        mciSendStringA(paused ? "resume " ALIAS : "pause " ALIAS, NULL, 0, NULL);
        paused = !paused;
        SetWindowTextW(pause_button, paused ? L"Resume" : L"Pause");
        update_position(NULL, 0, 0, 0);
        return 1;
    }
    if (id == IDC_AUDIO_STOP) {
        stop_playing();
        set_status(L"Stopped.");
        return 1;
    }
    return 0;
}

void audio_page_close(void) {
    stop_playing();
}
