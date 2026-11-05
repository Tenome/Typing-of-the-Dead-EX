#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "replays_page.h"

#define ROW_HEIGHT 24
#define BUTTON_HEIGHT 26
#define BUTTON_WIDTH 150
#define REPLAY_HEADER_BYTES 96
#define FRAME_COUNT_OFFSET 64
#define VERSION_OFFSET 12

enum {
    IDC_REPLAY_RECORD = 2600,
    IDC_REPLAY_LIST,
    IDC_REPLAY_WATCH,
    IDC_REPLAY_FOLDER,
    IDC_REPLAY_LABEL,
};

static HWND controls[8];
static int control_count;
static HWND record_box, list_box;
static char replay_folder[MAX_PATH], game_folder_path[MAX_PATH];

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                        int width, int height, int id) {
    HWND control = CreateWindowExW(lstrcmpW(class_name, WC_LISTBOXW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                   WS_CHILD | style, x, y, width, height, dialog, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static void describe_replay(const char *name, char *out, size_t size) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s%s", replay_folder, name);
    unsigned char header[REPLAY_HEADER_BYTES] = {0};
    FILE *file = fopen(path, "rb");
    if (file) {
        fread(header, 1, sizeof header, file);
        fclose(file);
    }
    unsigned frames;
    memcpy(&frames, header + FRAME_COUNT_OFFSET, sizeof frames);
    char version[17] = "";
    memcpy(version, header + VERSION_OFFSET, 16);
    unsigned seconds = frames / 60;
    snprintf(out, size, "%s    %u:%02u    todmod %s", name, seconds / 60, seconds % 60, version);
}

static void fill_replay_list(void) {
    SendMessageA(list_box, LB_RESETCONTENT, 0, 0);
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s*.todreplay", replay_folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        SendMessageA(list_box, LB_ADDSTRING, 0, (LPARAM)"(no replays yet: play a game with recording on)");
        return;
    }
    do {
        char line[256];
        describe_replay(found.cFileName, line, sizeof line);
        SendMessageA(list_box, LB_INSERTSTRING, 0, (LPARAM)line);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static void watch_selected(HWND dialog) {
    LRESULT index = SendMessageA(list_box, LB_GETCURSEL, 0, 0);
    if (index < 0) index = 0;
    char line[256] = "";
    SendMessageA(list_box, LB_GETTEXT, (WPARAM)index, (LPARAM)line);
    char *end = strstr(line, ".todreplay");
    if (!end) return;
    end[strlen(".todreplay")] = '\0';
    char game[MAX_PATH];
    snprintf(game, sizeof game, "%sTod.exe", game_folder_path);
    SetEnvironmentVariableA("TODMOD_REPLAY", line);
    STARTUPINFOA startup = {sizeof startup};
    PROCESS_INFORMATION process;
    if (CreateProcessA(game, NULL, NULL, NULL, FALSE, 0, NULL, game_folder_path, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        MessageBoxW(dialog, L"Tod.exe could not be started.", L"Replays", MB_ICONWARNING);
    }
    SetEnvironmentVariableA("TODMOD_REPLAY", NULL);
}

void replays_page_create(HWND dialog, RECT area, const char *game_folder, const ini_file *ini) {
    control_count = 0;
    lstrcpynA(game_folder_path, game_folder, MAX_PATH);
    snprintf(replay_folder, sizeof replay_folder, "%sreplays\\", game_folder);
    int x = area.left, y = area.top, width = area.right - area.left;
    record_box = add_control(dialog, WC_BUTTONW, L"Record every game (the newest 20 are kept in replays\\)",
                             BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, ROW_HEIGHT - 4, IDC_REPLAY_RECORD);
    SendMessageW(record_box, BM_SETCHECK, ini_get_int(ini, "replay", "record", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    y += ROW_HEIGHT;
    add_control(dialog, WC_STATICW,
                L"A replay stores only the keys of both players, the save and the random seed, so it plays back "
                L"exactly only with the same todmod version, word packs and settings (todmod.log warns otherwise). "
                L"While a replay runs the keyboard does nothing; when it ends you have control.",
                0, x, y, width, 48, IDC_REPLAY_LABEL);
    y += 52;
    int list_height = area.bottom - y - BUTTON_HEIGHT - 8;
    list_box = add_control(dialog, WC_LISTBOXW, L"", WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, x, y,
                           width, list_height, IDC_REPLAY_LIST);
    y += list_height + 8;
    add_control(dialog, WC_BUTTONW, L"Watch selected replay", WS_TABSTOP, x, y, BUTTON_WIDTH, BUTTON_HEIGHT, IDC_REPLAY_WATCH);
    add_control(dialog, WC_BUTTONW, L"Open replays folder", WS_TABSTOP, x + BUTTON_WIDTH + 8, y, BUTTON_WIDTH,
                BUTTON_HEIGHT, IDC_REPLAY_FOLDER);
    fill_replay_list();
    replays_page_show(0);
}

void replays_page_show(int visible) {
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
    if (visible) fill_replay_list();
}

void replays_page_save(ini_file *ini) {
    if (!control_count) return;
    ini_set_int(ini, "replay", "record", SendMessageW(record_box, BM_GETCHECK, 0, 0) == BST_CHECKED);
}

int replays_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    if (message != WM_COMMAND) return 0;
    if (LOWORD(wparam) == IDC_REPLAY_WATCH || (LOWORD(wparam) == IDC_REPLAY_LIST && HIWORD(wparam) == LBN_DBLCLK)) {
        watch_selected(dialog);
        return 1;
    }
    if (LOWORD(wparam) == IDC_REPLAY_FOLDER) {
        CreateDirectoryA(replay_folder, NULL);
        ShellExecuteA(NULL, "open", replay_folder, NULL, NULL, SW_SHOWNORMAL);
        return 1;
    }
    return 0;
}
