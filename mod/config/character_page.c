#include <windows.h>
#include <string.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "character_page.h"
#include "character_list.h"
#include "game_models.h"
#include "resource.h"
#include "subtitle_page.h"
#include "online_page.h"
#include "mutators_page.h"
#include "stats_page.h"
#include "replays_page.h"
#include "audio_page.h"

#define TAB_STRIP_HEIGHT 28
#define PAGE_MARGIN 10
#define LIST_WIDTH 300
#define BUTTON_HEIGHT 26
#define MAX_SETTINGS_CONTROLS 256
#define TURN_STEP 0.5235988f
#define NORMAL_CHARACTER (-1)

enum {
    IDC_PAGE_TABS = 2000,
    IDC_CHARACTER_LIST,
    IDC_CHARACTER_PREVIEW,
    IDC_TURN_LEFT,
    IDC_TURN_RIGHT,
    IDC_USE_PLAYER1,
    IDC_USE_PLAYER2,
    IDC_BOTH_NORMAL,
    IDC_CHOSEN_CHARACTERS,
    IDC_PREVIEW_NOTE,
};

static HWND settings_controls[MAX_SETTINGS_CONTROLS];
static int settings_control_count;
static HWND page_controls[16];
static int page_control_count;
static HWND tabs, character_list, preview;
static HBITMAP preview_bitmap;
static int models_ready;
static float preview_yaw;

static int is_shared_control(HWND control) {
    int id = GetDlgCtrlID(control);
    return id == IDC_OPEN_DICT || id == IDC_STATUS || id == IDC_SAVE_PLAY || id == IDC_SAVE || id == IDCANCEL;
}

static BOOL CALLBACK collect_settings_control(HWND control, LPARAM dialog) {
    if (GetParent(control) != (HWND)dialog || is_shared_control(control)) return TRUE;
    if (settings_control_count < MAX_SETTINGS_CONTROLS) settings_controls[settings_control_count++] = control;
    return TRUE;
}

static BOOL CALLBACK move_control_down(HWND control, LPARAM dialog) {
    if (GetParent(control) != (HWND)dialog) return TRUE;
    RECT rect;
    GetWindowRect(control, &rect);
    MapWindowPoints(NULL, (HWND)dialog, (POINT *)&rect, 2);
    SetWindowPos(control, NULL, rect.left, rect.top + TAB_STRIP_HEIGHT, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    return TRUE;
}

static HWND add_page_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                             int width, int height, int id) {
    HWND control = CreateWindowExW(lstrcmpW(class_name, WC_LISTBOXW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                   WS_CHILD | style, x, y, width, height, dialog, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    page_controls[page_control_count++] = control;
    return control;
}

static int selected_kind(void) {
    LRESULT index = SendMessageW(character_list, LB_GETCURSEL, 0, 0);
    return index == LB_ERR ? NORMAL_CHARACTER : (int)SendMessageW(character_list, LB_GETITEMDATA, (WPARAM)index, 0);
}

static void character_label(int kind, char *out, size_t size) {
    const char *pack_name = game_models_pack_name(kind);
    if (kind < 0 || kind >= CHARACTER_COUNT) snprintf(out, size, "Normal");
    else if (pack_name) snprintf(out, size, "%d: %s", kind, pack_name);
    else snprintf(out, size, "%d: %s", kind, character_names[kind]);
}

static void relabel_combo(HWND dialog, int control) {
    int chosen = (int)SendDlgItemMessageA(dialog, control, CB_GETCURSEL, 0, 0);
    LRESULT count = SendDlgItemMessageA(dialog, control, CB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; i++) {
        int kind = (int)SendDlgItemMessageA(dialog, control, CB_GETITEMDATA, (WPARAM)i, 0);
        char label[160];
        character_label(kind, label, sizeof label);
        SendDlgItemMessageA(dialog, control, CB_DELETESTRING, (WPARAM)i, 0);
        SendDlgItemMessageA(dialog, control, CB_INSERTSTRING, (WPARAM)i, (LPARAM)label);
        SendDlgItemMessageA(dialog, control, CB_SETITEMDATA, (WPARAM)i, (LPARAM)kind);
    }
    SendDlgItemMessageA(dialog, control, CB_SETCURSEL, (WPARAM)chosen, 0);
}

static void use_current_model_packs(HWND dialog) {
    char packs[512] = "";
    GetDlgItemTextA(dialog, IDC_MODEL_PACKS, packs, sizeof packs);
    game_models_set_packs(packs);
    int chosen = (int)SendMessageW(character_list, LB_GETCURSEL, 0, 0);
    LRESULT count = SendMessageW(character_list, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; i++) {
        int kind = (int)SendMessageW(character_list, LB_GETITEMDATA, (WPARAM)i, 0);
        if (kind < 0) continue;
        char label[160];
        character_label(kind, label, sizeof label);
        SendMessageA(character_list, LB_DELETESTRING, (WPARAM)i, 0);
        SendMessageA(character_list, LB_INSERTSTRING, (WPARAM)i, (LPARAM)label);
        SendMessageA(character_list, LB_SETITEMDATA, (WPARAM)i, (LPARAM)kind);
    }
    SendMessageW(character_list, LB_SETCURSEL, (WPARAM)chosen, 0);
    relabel_combo(dialog, IDC_PLAYER1_CHARACTER);
    relabel_combo(dialog, IDC_PLAYER2_CHARACTER);
}

static int combo_kind(HWND dialog, int control) {
    LRESULT index = SendDlgItemMessageA(dialog, control, CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? NORMAL_CHARACTER : (int)SendDlgItemMessageA(dialog, control, CB_GETITEMDATA, (WPARAM)index, 0);
}

static void set_combo_kind(HWND dialog, int control, int kind) {
    LRESULT count = SendDlgItemMessageA(dialog, control, CB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; i++) {
        if ((int)SendDlgItemMessageA(dialog, control, CB_GETITEMDATA, (WPARAM)i, 0) == kind) {
            SendDlgItemMessageA(dialog, control, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
}

static void show_chosen_characters(HWND dialog) {
    char first[160], second[160], text[400];
    character_label(combo_kind(dialog, IDC_PLAYER1_CHARACTER), first, sizeof first);
    character_label(combo_kind(dialog, IDC_PLAYER2_CHARACTER), second, sizeof second);
    snprintf(text, sizeof text, "Player 1: %s\r\nPlayer 2: %s\r\n(Save to apply.)", first, second);
    SetDlgItemTextA(dialog, IDC_CHOSEN_CHARACTERS, text);
}

static void render_preview(void) {
    RECT rect;
    GetClientRect(preview, &rect);
    int width = rect.right, height = rect.bottom;
    if (width <= 0 || height <= 0) return;
    BITMAPINFO info = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    void *bits = NULL;
    HBITMAP bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap) return;
    int kind = selected_kind();
    int drawn = models_ready ? game_models_render_character(kind < 0 ? 57 : kind, bits, width, height, preview_yaw) : 0;
    if (!drawn) {
        uint32_t *pixels = bits;
        for (int i = 0; i < width * height; i++) pixels[i] = 0xFF3A3A40u;
    }
    if (preview_bitmap) DeleteObject(preview_bitmap);
    preview_bitmap = bitmap;
    InvalidateRect(preview, NULL, FALSE);
}

static void show_page(HWND dialog, int page) {
    int characters = page == 1;
    for (int i = 0; i < settings_control_count; i++) ShowWindow(settings_controls[i], page == 0 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < page_control_count; i++) ShowWindow(page_controls[i], characters ? SW_SHOW : SW_HIDE);
    subtitle_page_show(page == 2);
    randomizer_page_show(page == 3);
    mutators_page_show(page == 4);
    online_page_show(page == 5);
    stats_page_show(page == 6);
    replays_page_show(page == 7);
    audio_page_show(page == 8);
    if (characters) {
        use_current_model_packs(dialog);
        show_chosen_characters(dialog);
        render_preview();
    }
}

void character_page_create(HWND dialog, const char *game_folder, const char *subtitle_file,
                           const randomizer_settings *random_settings, const ini_file *todmod_ini) {
    settings_control_count = page_control_count = 0;
    EnumChildWindows(dialog, collect_settings_control, (LPARAM)dialog);
    EnumChildWindows(dialog, move_control_down, (LPARAM)dialog);
    RECT window;
    GetWindowRect(dialog, &window);
    SetWindowPos(dialog, NULL, 0, 0, window.right - window.left, window.bottom - window.top + TAB_STRIP_HEIGHT,
                 SWP_NOMOVE | SWP_NOZORDER);
    RECT client, buttons;
    GetClientRect(dialog, &client);
    GetWindowRect(GetDlgItem(dialog, IDC_SAVE), &buttons);
    MapWindowPoints(NULL, dialog, (POINT *)&buttons, 2);

    tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, PAGE_MARGIN, 4,
                           client.right - 2 * PAGE_MARGIN, TAB_STRIP_HEIGHT - 4, dialog, (HMENU)(INT_PTR)IDC_PAGE_TABS,
                           GetModuleHandleW(NULL), NULL);
    SendMessageW(tabs, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    TCITEMW item = {TCIF_TEXT};
    item.pszText = L"Settings";
    SendMessageW(tabs, TCM_INSERTITEMW, 0, (LPARAM)&item);
    item.pszText = L"Characters";
    SendMessageW(tabs, TCM_INSERTITEMW, 1, (LPARAM)&item);
    item.pszText = L"Subtitles";
    SendMessageW(tabs, TCM_INSERTITEMW, 2, (LPARAM)&item);
    item.pszText = L"Randomizer";
    SendMessageW(tabs, TCM_INSERTITEMW, 3, (LPARAM)&item);
    item.pszText = L"Mutators";
    SendMessageW(tabs, TCM_INSERTITEMW, 4, (LPARAM)&item);
    item.pszText = L"Online";
    SendMessageW(tabs, TCM_INSERTITEMW, 5, (LPARAM)&item);
    item.pszText = L"Stats";
    SendMessageW(tabs, TCM_INSERTITEMW, 6, (LPARAM)&item);
    item.pszText = L"Replays";
    SendMessageW(tabs, TCM_INSERTITEMW, 7, (LPARAM)&item);
    item.pszText = L"Audio";
    SendMessageW(tabs, TCM_INSERTITEMW, 8, (LPARAM)&item);

    int top = TAB_STRIP_HEIGHT + PAGE_MARGIN, bottom = buttons.top - PAGE_MARGIN;
    int right_x = PAGE_MARGIN * 2 + LIST_WIDTH, right_width = client.right - right_x - PAGE_MARGIN;
    character_list = add_page_control(dialog, WC_LISTBOXW, L"", WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                      PAGE_MARGIN, top, LIST_WIDTH, bottom - top, IDC_CHARACTER_LIST);
    int info_height = 3 * BUTTON_HEIGHT + 3 * PAGE_MARGIN + 72;
    preview = add_page_control(dialog, WC_STATICW, L"", SS_OWNERDRAW, right_x, top, right_width,
                               bottom - top - info_height, IDC_CHARACTER_PREVIEW);
    int y = bottom - info_height + PAGE_MARGIN;
    int half = (right_width - PAGE_MARGIN) / 2;
    add_page_control(dialog, WC_BUTTONW, L"◀ Turn", WS_TABSTOP, right_x, y, half, BUTTON_HEIGHT, IDC_TURN_LEFT);
    add_page_control(dialog, WC_BUTTONW, L"Turn ▶", WS_TABSTOP, right_x + half + PAGE_MARGIN, y, half, BUTTON_HEIGHT,
                     IDC_TURN_RIGHT);
    y += BUTTON_HEIGHT + PAGE_MARGIN;
    add_page_control(dialog, WC_BUTTONW, L"Play as this: Player 1", WS_TABSTOP, right_x, y, half, BUTTON_HEIGHT,
                     IDC_USE_PLAYER1);
    add_page_control(dialog, WC_BUTTONW, L"Play as this: Player 2", WS_TABSTOP, right_x + half + PAGE_MARGIN, y, half,
                     BUTTON_HEIGHT, IDC_USE_PLAYER2);
    y += BUTTON_HEIGHT + PAGE_MARGIN;
    add_page_control(dialog, WC_BUTTONW, L"Both players normal", WS_TABSTOP, right_x, y, half, BUTTON_HEIGHT, IDC_BOTH_NORMAL);
    add_page_control(dialog, WC_STATICW, L"", 0, right_x + half + PAGE_MARGIN, y - 2, half, 48, IDC_CHOSEN_CHARACTERS);
    add_page_control(dialog, WC_STATICW,
                     L"Drawn from your game files (no waist joints). Bosses show unposed parts. Characters that "
                     L"would not show up in game are left out.",
                     0, right_x, y + BUTTON_HEIGHT + 22, right_width, 40, IDC_PREVIEW_NOTE);

    SendMessageA(character_list, LB_SETITEMDATA, SendMessageA(character_list, LB_ADDSTRING, 0, (LPARAM)"Normal (preview: player 1)"),
                 (LPARAM)NORMAL_CHARACTER);
    for (int kind = 0; kind < CHARACTER_COUNT; kind++) {
        if (strstr(character_names[kind], "may be invisible")) continue;
        char label[160];
        character_label(kind, label, sizeof label);
        LRESULT index = SendMessageA(character_list, LB_ADDSTRING, 0, (LPARAM)label);
        SendMessageA(character_list, LB_SETITEMDATA, (WPARAM)index, (LPARAM)kind);
    }
    SendMessageA(character_list, LB_SETCURSEL, 0, 0);
    models_ready = game_models_open(game_folder);
    RECT subtitle_area = {PAGE_MARGIN, top, client.right - PAGE_MARGIN, bottom};
    subtitle_page_create(dialog, game_folder, subtitle_area, subtitle_file);
    randomizer_page_create(dialog, subtitle_area, random_settings);
    mutators_page_create(dialog, subtitle_area, todmod_ini);
    online_page_create(dialog, subtitle_area, todmod_ini);
    stats_page_create(dialog, subtitle_area, game_folder);
    replays_page_create(dialog, subtitle_area, game_folder, todmod_ini);
    audio_page_create(dialog, subtitle_area, game_folder);
    show_page(dialog, 0);
}

void character_page_open_tab(HWND dialog, int page) {
    SendMessageW(tabs, TCM_SETCURSEL, (WPARAM)page, 0);
    show_page(dialog, page);
}

void character_page_open(HWND dialog, int kind) {
    SendMessageW(tabs, TCM_SETCURSEL, 1, 0);
    LRESULT count = SendMessageW(character_list, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; i++) {
        if ((int)SendMessageW(character_list, LB_GETITEMDATA, (WPARAM)i, 0) == kind) {
            SendMessageW(character_list, LB_SETCURSEL, (WPARAM)i, 0);
            break;
        }
    }
    show_page(dialog, 1);
}

void character_page_destroy(void) {
    if (preview_bitmap) DeleteObject(preview_bitmap);
    preview_bitmap = NULL;
    game_models_close();
}

int character_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NOTIFY && ((NMHDR *)lparam)->idFrom == IDC_PAGE_TABS && ((NMHDR *)lparam)->code == TCN_SELCHANGE) {
        show_page(dialog, (int)SendMessageW(tabs, TCM_GETCURSEL, 0, 0));
        return 1;
    }
    if (subtitle_page_message(dialog, message, wparam, lparam)) return 1;
    if (randomizer_page_message(dialog, message, wparam, lparam)) return 1;
    if (online_page_message(dialog, message, wparam, lparam)) return 1;
    if (stats_page_message(dialog, message, wparam, lparam)) return 1;
    if (replays_page_message(dialog, message, wparam, lparam)) return 1;
    if (audio_page_message(dialog, message, wparam, lparam)) return 1;
    if (message == WM_DRAWITEM && wparam == IDC_CHARACTER_PREVIEW) {
        const DRAWITEMSTRUCT *draw = (const DRAWITEMSTRUCT *)lparam;
        RECT rect = draw->rcItem;
        if (!preview_bitmap) {
            FillRect(draw->hDC, &rect, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
        } else {
            HDC memory = CreateCompatibleDC(draw->hDC);
            HGDIOBJ previous = SelectObject(memory, preview_bitmap);
            BitBlt(draw->hDC, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
            DeleteDC(memory);
        }
        if (!models_ready) {
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, RGB(230, 230, 230));
            DrawTextW(draw->hDC, L"Previews need pol.HAB, tex.HAB and Table.dll next to this program.", -1, &rect,
                      DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
        return 1;
    }
    if (message != WM_COMMAND) return 0;
    switch (LOWORD(wparam)) {
    case IDC_CHARACTER_LIST:
        if (HIWORD(wparam) == LBN_SELCHANGE) {
            preview_yaw = 0;
            render_preview();
        } else if (HIWORD(wparam) == LBN_DBLCLK) {
            set_combo_kind(dialog, IDC_PLAYER1_CHARACTER, selected_kind());
            show_chosen_characters(dialog);
        }
        return 1;
    case IDC_TURN_LEFT:
    case IDC_TURN_RIGHT:
        preview_yaw += LOWORD(wparam) == IDC_TURN_LEFT ? -TURN_STEP : TURN_STEP;
        render_preview();
        return 1;
    case IDC_USE_PLAYER1:
    case IDC_USE_PLAYER2:
        set_combo_kind(dialog, LOWORD(wparam) == IDC_USE_PLAYER1 ? IDC_PLAYER1_CHARACTER : IDC_PLAYER2_CHARACTER,
                       selected_kind());
        show_chosen_characters(dialog);
        return 1;
    case IDC_BOTH_NORMAL:
        set_combo_kind(dialog, IDC_PLAYER1_CHARACTER, NORMAL_CHARACTER);
        set_combo_kind(dialog, IDC_PLAYER2_CHARACTER, NORMAL_CHARACTER);
        show_chosen_characters(dialog);
        return 1;
    }
    return 0;
}
