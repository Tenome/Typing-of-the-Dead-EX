#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include "online_page.h"

#define ROW_HEIGHT 24
#define LABEL_WIDTH 190
#define FIELD_WIDTH 220
#define MAX_DELAY 15

enum {
    IDC_ONLINE_OFF = 2300,
    IDC_ONLINE_HOST,
    IDC_ONLINE_JOIN,
    IDC_ONLINE_PEER,
    IDC_ONLINE_PORT,
    IDC_ONLINE_RELAY,
    IDC_ONLINE_ROOM,
    IDC_ONLINE_DELAY,
    IDC_ONLINE_LABEL,
};

static HWND controls[24];
static int control_count;
static HWND off_button, host_button, join_button, peer_box, port_box, relay_box, room_box, delay_box;

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                        int width, int height, int id) {
    HWND control = CreateWindowExW(lstrcmpW(class_name, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                   WS_CHILD | style, x, y, width, height, dialog, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static HWND add_labelled_edit(HWND dialog, const wchar_t *label, const char *value, DWORD style, int x, int y, int id) {
    add_control(dialog, WC_STATICW, label, 0, x, y + 3, LABEL_WIDTH, 18, IDC_ONLINE_LABEL);
    HWND box = add_control(dialog, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP | style, x + LABEL_WIDTH, y, FIELD_WIDTH,
                           ROW_HEIGHT - 2, id);
    SetWindowTextA(box, value);
    return box;
}

static int checked(HWND button) {
    return SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void update_fields(void) {
    int on = !checked(off_button);
    EnableWindow(peer_box, on && checked(join_button));
    HWND rest[] = {port_box, relay_box, room_box, delay_box};
    for (size_t i = 0; i < sizeof rest / sizeof rest[0]; i++) EnableWindow(rest[i], on);
}

void online_page_create(HWND dialog, RECT area, const ini_file *ini) {
    control_count = 0;
    int x = area.left, y = area.top, width = area.right - area.left;
    char mode[16] = "off", peer[128] = "127.0.0.1", relay[128] = "", room[64] = "", delay[16] = "auto";
    ini_get(ini, "online", "mode", mode, sizeof mode);
    ini_get(ini, "online", "peer", peer, sizeof peer);
    ini_get(ini, "online", "relay", relay, sizeof relay);
    ini_get(ini, "online", "room", room, sizeof room);
    ini_get(ini, "online", "input_delay", delay, sizeof delay);
    char port[16];
    snprintf(port, sizeof port, "%d", ini_get_int(ini, "online", "port", 7845));

    add_control(dialog, WC_STATICW, L"Online play (experimental): two PCs play Arcade together, one as each player.", 0,
                x, y, width, 18, IDC_ONLINE_LABEL);
    y += ROW_HEIGHT;
    off_button = add_control(dialog, WC_BUTTONW, L"Off", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, x, y, 60,
                             ROW_HEIGHT - 4, IDC_ONLINE_OFF);
    host_button = add_control(dialog, WC_BUTTONW, L"Host (I am player 1)", BS_AUTORADIOBUTTON, x + 70, y, 160,
                              ROW_HEIGHT - 4, IDC_ONLINE_HOST);
    join_button = add_control(dialog, WC_BUTTONW, L"Join (I am player 2)", BS_AUTORADIOBUTTON, x + 240, y, 160,
                              ROW_HEIGHT - 4, IDC_ONLINE_JOIN);
    HWND chosen = !lstrcmpiA(mode, "host") ? host_button : !lstrcmpiA(mode, "join") ? join_button : off_button;
    SendMessageW(chosen, BM_SETCHECK, BST_CHECKED, 0);
    y += ROW_HEIGHT + 4;
    peer_box = add_labelled_edit(dialog, L"Host's address (when joining):", peer, 0, x, y, IDC_ONLINE_PEER);
    y += ROW_HEIGHT;
    port_box = add_labelled_edit(dialog, L"UDP port:", port, ES_NUMBER, x, y, IDC_ONLINE_PORT);
    y += ROW_HEIGHT;
    relay_box = add_labelled_edit(dialog, L"Relay server (optional, name:port):", relay, 0, x, y, IDC_ONLINE_RELAY);
    y += ROW_HEIGHT;
    room_box = add_labelled_edit(dialog, L"Room name (with a relay):", room, 0, x, y, IDC_ONLINE_ROOM);
    y += ROW_HEIGHT;
    add_control(dialog, WC_STATICW, L"Input delay (frames):", 0, x, y + 3, LABEL_WIDTH, 18, IDC_ONLINE_LABEL);
    delay_box = add_control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + LABEL_WIDTH, y, 120,
                            300, IDC_ONLINE_DELAY);
    SendMessageW(delay_box, CB_ADDSTRING, 0, (LPARAM)L"Auto (from ping)");
    for (int i = 0; i <= MAX_DELAY; i++) {
        wchar_t text[8];
        swprintf(text, 8, L"%d", i);
        SendMessageW(delay_box, CB_ADDSTRING, 0, (LPARAM)text);
    }
    int fixed = lstrcmpiA(delay, "auto") && delay[0] ? atoi(delay) : -1;
    if (fixed > MAX_DELAY) fixed = MAX_DELAY;
    SendMessageW(delay_box, CB_SETCURSEL, (WPARAM)(fixed < 0 ? 0 : fixed + 1), 0);
    y += ROW_HEIGHT + 10;
    add_control(dialog, WC_STATICW,
                L"Both PCs need the same todmod version, word packs, typing settings, cheats/randomizer, model packs "
                L"and Tod.ini game settings; the game refuses online play otherwise (todmod.log says what differs). "
                L"The joining PC plays on the host's save and leaves its own untouched.\r\n\r\n"
                L"Connecting: the host forwards the UDP port on the router, or both use a VPN such as Tailscale or "
                L"ZeroTier (then enter the host's VPN address), or both enter the same relay server and room "
                L"(the patch's tools\\online_relay.py runs one). Start both games; each waits for the other "
                L"(Esc = play offline). The window title shows the ping and any problem.",
                0, x, y, width, 150, IDC_ONLINE_LABEL);
    update_fields();
    online_page_show(0);
}

void online_page_show(int visible) {
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
}

static void save_text(ini_file *ini, const char *key, HWND box) {
    char text[128] = "";
    GetWindowTextA(box, text, sizeof text);
    ini_set(ini, "online", key, text);
}

void online_page_save(ini_file *ini) {
    if (!control_count) return;
    ini_set(ini, "online", "mode", checked(host_button) ? "host" : checked(join_button) ? "join" : "off");
    save_text(ini, "peer", peer_box);
    save_text(ini, "port", port_box);
    save_text(ini, "relay", relay_box);
    save_text(ini, "room", room_box);
    LRESULT delay = SendMessageW(delay_box, CB_GETCURSEL, 0, 0);
    if (delay <= 0) {
        ini_set(ini, "online", "input_delay", "auto");
    } else {
        ini_set_int(ini, "online", "input_delay", (int)delay - 1);
    }
}

int online_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)dialog;
    (void)lparam;
    if (message == WM_COMMAND && LOWORD(wparam) >= IDC_ONLINE_OFF && LOWORD(wparam) <= IDC_ONLINE_JOIN) {
        update_fields();
        return 1;
    }
    return 0;
}
