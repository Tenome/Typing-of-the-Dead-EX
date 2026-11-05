#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "randomizer_page.h"

#define ROW_HEIGHT 22
#define INDENT 20

enum {
    IDC_RANDOM_ENABLED = 2200,
    IDC_RANDOM_ENEMIES,
    IDC_RANDOM_PEOPLE,
    IDC_RANDOM_MIX,
    IDC_RANDOM_VOICES,
    IDC_RANDOM_ITEMS,
    IDC_RANDOM_WEAPONS,
    IDC_RANDOM_SEED_LABEL,
    IDC_RANDOM_SEED,
    IDC_RANDOM_NOTE,
    IDC_RANDOM_HUMANS_LABEL,
    IDC_RANDOM_HUMANS,
    IDC_RANDOM_COUNT_LABEL,
    IDC_RANDOM_COUNT_MIN,
    IDC_RANDOM_COUNT_TO,
    IDC_RANDOM_COUNT_MAX,
};

static const struct {
    const wchar_t *name;
    int percent;
} human_choices[] = {{L"none", 0}, {L"a few (25 %)", 25}, {L"half (50 %)", 50}, {L"all", 100}};

static HWND controls[24];
static int control_count;
static HWND enabled_box, enemies_box, people_box, mix_box, voices_box, items_box, weapons_box, seed_box, humans_box, count_min_box, count_max_box;

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                        int width, int height, int id) {
    HWND control = CreateWindowExW(lstrcmpW(class_name, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                   WS_CHILD | style, x, y, width, height, dialog, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static HWND add_check(HWND dialog, const wchar_t *text, int x, int y, int width, int id, int checked) {
    HWND box = add_control(dialog, WC_BUTTONW, text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, ROW_HEIGHT - 4, id);
    SendMessageW(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return box;
}

static HWND add_combo(HWND dialog, int x, int y, int width, int id) {
    return add_control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y - 2, width, 200, id);
}

static void add_count_choices(HWND combo, int first, int selected) {
    for (int count = first; count <= 4; count++) {
        wchar_t text[8];
        swprintf(text, 8, L"%d", count);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)(selected - first), 0);
}

static int checked(HWND box) {
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void update_enabled(void) {
    BOOL on = checked(enabled_box);
    HWND parts[] = {enemies_box, people_box, mix_box, voices_box, items_box, weapons_box, seed_box, humans_box,
                    count_min_box, count_max_box};
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) EnableWindow(parts[i], on);
}

void randomizer_page_create(HWND dialog, RECT area, const randomizer_settings *settings) {
    control_count = 0;
    int x = area.left, y = area.top, width = area.right - area.left;
    enabled_box = add_check(dialog, L"Randomize each game", x, y, width, IDC_RANDOM_ENABLED, settings->enabled);
    y += ROW_HEIGHT + 4;
    enemies_box = add_check(dialog, L"Zombie looks: each zombie type looks like another zombie type", x + INDENT, y,
                            width - INDENT, IDC_RANDOM_ENEMIES, settings->enemies);
    y += ROW_HEIGHT;
    people_box = add_check(dialog, L"People looks: civilians and story characters look like other people", x + INDENT, y,
                           width - INDENT, IDC_RANDOM_PEOPLE, settings->people);
    y += ROW_HEIGHT;
    mix_box = add_check(dialog, L"Mix both: people may look like zombies and zombies like people", x + INDENT, y,
                        width - INDENT, IDC_RANDOM_MIX, settings->mix);
    y += ROW_HEIGHT;
    voices_box = add_check(dialog, L"Voice lines: scenes speak other lines (subtitles match the voice)", x + INDENT, y,
                           width - INDENT, IDC_RANDOM_VOICES, settings->voices);
    y += ROW_HEIGHT;
    items_box = add_check(dialog, L"Items: a picked-up item has a random effect", x + INDENT, y, width - INDENT,
                          IDC_RANDOM_ITEMS, settings->items);
    y += ROW_HEIGHT;
    weapons_box = add_check(dialog, L"Weapons: thrown axes, hammers and the like become other weapons (even a chainsaw)",
                            x + INDENT, y, width - INDENT, IDC_RANDOM_WEAPONS, settings->weapons);
    y += ROW_HEIGHT + 4;
    add_control(dialog, WC_STATICW, L"Zombie types that look like people:", 0, x + INDENT, y + 1, 220, 18,
                IDC_RANDOM_HUMANS_LABEL);
    humans_box = add_combo(dialog, x + INDENT + 225, y, 120, IDC_RANDOM_HUMANS);
    int human_selected = 0;
    for (int i = 0; i < (int)(sizeof human_choices / sizeof *human_choices); i++) {
        SendMessageW(humans_box, CB_ADDSTRING, 0, (LPARAM)human_choices[i].name);
        if (settings->human_zombies >= human_choices[i].percent) human_selected = i;
    }
    SendMessageW(humans_box, CB_SETCURSEL, (WPARAM)human_selected, 0);
    y += ROW_HEIGHT + 4;
    add_control(dialog, WC_STATICW, L"Each zombie appears this many times:", 0, x + INDENT, y + 1, 220, 18,
                IDC_RANDOM_COUNT_LABEL);
    count_min_box = add_combo(dialog, x + INDENT + 225, y, 45, IDC_RANDOM_COUNT_MIN);
    add_count_choices(count_min_box, 0, settings->enemy_min < 0 ? 0 : settings->enemy_min > 4 ? 4 : settings->enemy_min);
    add_control(dialog, WC_STATICW, L"to", 0, x + INDENT + 276, y + 1, 18, 18, IDC_RANDOM_COUNT_TO);
    count_max_box = add_combo(dialog, x + INDENT + 298, y, 45, IDC_RANDOM_COUNT_MAX);
    add_count_choices(count_max_box, 1, settings->enemy_max < 1 ? 1 : settings->enemy_max > 4 ? 4 : settings->enemy_max);
    y += ROW_HEIGHT + 8;
    add_control(dialog, WC_STATICW, L"Seed (0 = a new mix every launch):", 0, x + INDENT, y + 3, 200, 18,
                IDC_RANDOM_SEED_LABEL);
    wchar_t seed[16];
    swprintf(seed, 16, L"%u", settings->seed);
    seed_box = add_control(dialog, WC_EDITW, seed, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, x + INDENT + 205, y, 100,
                           ROW_HEIGHT, IDC_RANDOM_SEED);
    y += ROW_HEIGHT + 12;
    add_control(dialog, WC_STATICW,
                L"The seed of each game is written to todmod.log (\"randomizer: seed ...\"); enter it here to replay "
                L"the same mix. Looks follow the model packs in use, so House of the Dead models join the mix. "
                L"Characters keep their own moves, words and strength; only how they look changes. "
                L"Extra zombies appear beside the original; 0 lets some zombies stay away (one of each group always "
                L"comes). Bosses are never doubled.",
                0, x, y, width, 84, IDC_RANDOM_NOTE);
    update_enabled();
    randomizer_page_show(0);
}

void randomizer_page_show(int visible) {
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
}

void randomizer_page_settings(randomizer_settings *settings) {
    settings->enabled = checked(enabled_box);
    settings->enemies = checked(enemies_box);
    settings->people = checked(people_box);
    settings->mix = checked(mix_box);
    settings->voices = checked(voices_box);
    settings->items = checked(items_box);
    settings->weapons = checked(weapons_box);
    wchar_t seed[16] = L"";
    GetWindowTextW(seed_box, seed, 16);
    settings->seed = (unsigned)wcstoul(seed, NULL, 10);
    LRESULT human = SendMessageW(humans_box, CB_GETCURSEL, 0, 0);
    settings->human_zombies = human >= 0 ? human_choices[human].percent : 0;
    settings->enemy_min = (int)SendMessageW(count_min_box, CB_GETCURSEL, 0, 0);
    settings->enemy_max = (int)SendMessageW(count_max_box, CB_GETCURSEL, 0, 0) + 1;
    if (settings->enemy_min < 0) settings->enemy_min = 1;
    if (settings->enemy_max < settings->enemy_min) settings->enemy_max = settings->enemy_min < 1 ? 1 : settings->enemy_min;
}

int randomizer_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)dialog;
    (void)lparam;
    if (message == WM_COMMAND && LOWORD(wparam) == IDC_RANDOM_ENABLED) {
        update_enabled();
        return 1;
    }
    return 0;
}
