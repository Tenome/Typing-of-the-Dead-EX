#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include "mutators_page.h"

#define ROW_HEIGHT 26
#define LABEL_WIDTH 170
#define FIELD_WIDTH 300

enum {
    IDC_MUTATOR_LIVES = 2400,
    IDC_MUTATOR_TYPOS,
    IDC_MUTATOR_LEVEL,
    IDC_MUTATOR_WORDS,
    IDC_MUTATOR_CASE,
    IDC_MUTATOR_SPEED,
    IDC_MUTATOR_LABEL,
};

typedef struct {
    const wchar_t *label;
    const char *value;
} choice;

static const choice lives_choices[] = {{L"Normal", "0"}, {L"1 (one hit and it's over)", "1"}, {L"2", "2"}};
static const choice level_choices[] = {
    {L"Normal (the game adapts)", "normal"}, {L"Always the easiest words", "easiest"}, {L"Always the hardest words", "hardest"}};
static const choice word_choices[] = {
    {L"Normal", "normal"}, {L"Reversed (type them backwards)", "reversed"},
    {L"Scrambled (first and last letters stay)", "scrambled"}};
static const choice case_choices[] = {
    {L"Normal", "normal"}, {L"ALL CAPITALS", "upper"}, {L"all small letters", "lower"}, {L"RaNdOm CaSe", "random"}};
static const choice speed_choices[] = {{L"50% (slow motion)", "50"}, {L"75%", "75"}, {L"100% (normal)", "100"},
                                       {L"125%", "125"}, {L"150%", "150"}, {L"200% (frantic)", "200"}};

static HWND controls[24];
static int control_count;
static HWND lives_box, typos_box, level_box, words_box, case_box, speed_box;

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                        int width, int height, int id) {
    HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | style, x, y, width, height, dialog,
                                   (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static HWND add_choice(HWND dialog, const wchar_t *label, const choice *choices, int count, const char *current,
                       int x, int y, int id) {
    add_control(dialog, WC_STATICW, label, 0, x, y + 3, LABEL_WIDTH, 18, IDC_MUTATOR_LABEL);
    HWND box = add_control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + LABEL_WIDTH, y,
                           FIELD_WIDTH, 200, id);
    int selected = 0;
    for (int i = 0; i < count; i++) {
        SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)choices[i].label);
        if (!lstrcmpiA(choices[i].value, current)) selected = i;
    }
    SendMessageW(box, CB_SETCURSEL, (WPARAM)selected, 0);
    return box;
}

#define CHOICES(list) list, (int)(sizeof list / sizeof list[0])

void mutators_page_create(HWND dialog, RECT area, const ini_file *ini) {
    control_count = 0;
    int x = area.left, y = area.top, width = area.right - area.left;
    char lives[8] = "0", level[16] = "normal", words[16] = "normal", letter_case[16] = "normal", speed[8] = "100";
    ini_get(ini, "mutators", "max_lives", lives, sizeof lives);
    ini_get(ini, "mutators", "word_level", level, sizeof level);
    ini_get(ini, "mutators", "words", words, sizeof words);
    ini_get(ini, "mutators", "letter_case", letter_case, sizeof letter_case);
    ini_get(ini, "mutators", "speed", speed, sizeof speed);

    add_control(dialog, WC_STATICW, L"Mutators change the rules for a harder, stranger or easier game.", 0, x, y, width,
                18, IDC_MUTATOR_LABEL);
    y += ROW_HEIGHT;
    lives_box = add_choice(dialog, L"Most lives:", CHOICES(lives_choices), lives, x, y, IDC_MUTATOR_LIVES);
    y += ROW_HEIGHT;
    typos_box = add_control(dialog, WC_BUTTONW, L"Typos cost a life", BS_AUTOCHECKBOX | WS_TABSTOP, x + LABEL_WIDTH, y,
                            FIELD_WIDTH, ROW_HEIGHT - 6, IDC_MUTATOR_TYPOS);
    SendMessageW(typos_box, BM_SETCHECK, ini_get_int(ini, "mutators", "typos_hurt", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    y += ROW_HEIGHT;
    level_box = add_choice(dialog, L"Word difficulty:", CHOICES(level_choices), level, x, y, IDC_MUTATOR_LEVEL);
    y += ROW_HEIGHT;
    words_box = add_choice(dialog, L"Words:", CHOICES(word_choices), words, x, y, IDC_MUTATOR_WORDS);
    y += ROW_HEIGHT;
    case_box = add_choice(dialog, L"Letter case:", CHOICES(case_choices), letter_case, x, y, IDC_MUTATOR_CASE);
    y += ROW_HEIGHT;
    speed_box = add_choice(dialog, L"Game speed:", CHOICES(speed_choices), speed, x, y, IDC_MUTATOR_SPEED);
    y += ROW_HEIGHT + 10;
    add_control(dialog, WC_STATICW,
                L"Words and letter case change the words of your word packs (words.tsv), not the game's own "
                L"word.HAB. Letter case only matters with \"Capital letters must match\" on the Settings page. "
                L"A typo costs a life at most once per hit pause (about 1.5 s). Game speed is ignored when "
                L"[timing] update_rate is set by hand.",
                0, x, y, width, 80, IDC_MUTATOR_LABEL);
    mutators_page_show(0);
}

void mutators_page_show(int visible) {
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
}

static const char *selected_value(HWND box, const choice *choices, int count) {
    LRESULT index = SendMessageW(box, CB_GETCURSEL, 0, 0);
    return index >= 0 && index < count ? choices[index].value : choices[0].value;
}

void mutators_page_save(ini_file *ini) {
    if (!control_count) return;
    ini_set(ini, "mutators", "max_lives", selected_value(lives_box, CHOICES(lives_choices)));
    ini_set_int(ini, "mutators", "typos_hurt", SendMessageW(typos_box, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ini_set(ini, "mutators", "word_level", selected_value(level_box, CHOICES(level_choices)));
    ini_set(ini, "mutators", "words", selected_value(words_box, CHOICES(word_choices)));
    ini_set(ini, "mutators", "letter_case", selected_value(case_box, CHOICES(case_choices)));
    ini_set(ini, "mutators", "speed", selected_value(speed_box, CHOICES(speed_choices)));
}
