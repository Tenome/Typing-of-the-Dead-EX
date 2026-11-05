#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stats_page.h"
#include "achievement_list.h"
#include "pack_builder.h"

#define BUTTON_HEIGHT 26
#define BUTTON_WIDTH 170
#define HISTORY_ROWS 20
#define MAX_TEXT 65536

enum {
    IDC_STATS_TEXT = 2500,
    IDC_STATS_OPEN_FOLDER,
    IDC_STATS_REFRESH,
    IDC_STATS_LABEL,
    IDC_STATS_PRACTICE,
};

#define MAX_PRACTICE_ENTRIES 20000
#define PRACTICE_WORDS 300

typedef struct {
    char word[64];
    int typos, count;
    double seconds;
} practice_entry;

static void (*practice_pack_ready)(const char *pack_name);
static char dict_folder_path[MAX_PATH];

static HWND controls[8];
static int control_count;
static HWND text_box;
static HFONT fixed_font;
static char stats_folder[MAX_PATH];

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y,
                        int width, int height, int id) {
    HWND control = CreateWindowExW(lstrcmpW(class_name, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                   WS_CHILD | style, x, y, width, height, dialog, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static char *read_text_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = malloc(MAX_TEXT);
    size_t got = text ? fread(text, 1, MAX_TEXT - 1, file) : 0;
    fclose(file);
    if (text) text[got] = '\0';
    return text;
}

static void append(char *out, size_t *used, const char *text) {
    size_t length = strlen(text);
    if (*used + length + 1 >= MAX_TEXT) return;
    memcpy(out + *used, text, length + 1);
    *used += length;
}

static void append_history(char *out, size_t *used) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%shistory.tsv", stats_folder);
    char *history = read_text_file(path);
    if (!history) {
        append(out, used, "No games recorded yet. Play a game; the statistics appear here after you close it.\r\n");
        return;
    }
    char *lines[512];
    int count = 0;
    for (char *line = strtok(history, "\r\n"); line && count < 512; line = strtok(NULL, "\r\n")) lines[count++] = line;
    append(out, used, "Recent games\r\n");
    append(out, used, "Session              Player  Words   Keys  Typos  Accuracy    WPM  Most missed\r\n");
    for (int i = count > HISTORY_ROWS + 1 ? count - HISTORY_ROWS : 1; i < count; i++) {
        char *fields[8] = {0};
        int field_count = 0;
        for (char *cursor = lines[i]; cursor && field_count < 8;) {
            fields[field_count++] = cursor;
            char *tab = strchr(cursor, '\t');
            if (tab) *tab = '\0';
            cursor = tab ? tab + 1 : NULL;
        }
        if (field_count < 7) continue;
        char row[512];
        snprintf(row, sizeof row, "%-20s %6s %6s %6s %6s %8s%% %6s  %s\r\n", fields[0], fields[1], fields[2], fields[3],
                 fields[4], fields[5], fields[6], fields[7] ? fields[7] : "");
        append(out, used, row);
    }
    free(history);
}

static void append_latest_report(char *out, size_t *used) {
    char pattern[MAX_PATH], latest[MAX_PATH] = "";
    snprintf(pattern, sizeof pattern, "%s20*_*.txt", stats_folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(found.cFileName, latest) > 0) lstrcpynA(latest, found.cFileName, MAX_PATH);
    } while (FindNextFileA(search, &found));
    FindClose(search);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s%s", stats_folder, latest);
    char *report = read_text_file(path);
    if (!report) return;
    append(out, used, "\r\nLast game (");
    append(out, used, latest);
    append(out, used, ")\r\n");
    for (char *line = report; *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        if (!strncmp(line, "Every word", 10)) break;
        append(out, used, line);
        append(out, used, "\r\n");
        if (!end) break;
        line = end + 1;
    }
    free(report);
}

static void append_achievements(char *out, size_t *used) {
    char path[MAX_PATH], line[512];
    snprintf(path, sizeof path, "%sachievements.tsv", stats_folder);
    char dates[ACHIEVEMENT_COUNT][32] = {{0}};
    FILE *file = fopen(path, "r");
    int count = 0;
    while (file && fgets(line, sizeof line, file)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *date = tab + 1;
        date[strcspn(date, "\t\r\n")] = '\0';
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            if (strcmp(achievement_list[i].id, line) || dates[i][0]) continue;
            snprintf(dates[i], sizeof dates[i], "%s", date);
            count++;
        }
    }
    if (file) fclose(file);
    char text[256];
    snprintf(text, sizeof text, "ACHIEVEMENTS (%d of %d)\r\n", count, ACHIEVEMENT_COUNT);
    append(out, used, text);
    for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
        snprintf(text, sizeof text, "  %s %-24s %-60s %s\r\n", dates[i][0] ? "[x]" : "[ ]", achievement_list[i].name,
                 achievement_list[i].description, dates[i]);
        append(out, used, text);
    }
    append(out, used, "\r\n");
}

static int compare_practice_words(const void *a, const void *b) {
    return strcmp(((const practice_entry *)a)->word, ((const practice_entry *)b)->word);
}

static double seconds_per_letter(const practice_entry *entry) {
    size_t letters = strlen(entry->word);
    return entry->count && letters ? entry->seconds / entry->count / (double)letters : 0.0;
}

static int compare_practice_need(const void *a, const void *b) {
    const practice_entry *left = a, *right = b;
    if (left->typos != right->typos) return right->typos - left->typos;
    double l = seconds_per_letter(left), r = seconds_per_letter(right);
    return l < r ? 1 : l > r ? -1 : 0;
}

static int collect_practice_words(practice_entry *entries) {
    char pattern[MAX_PATH], path[MAX_PATH], line[512];
    snprintf(pattern, sizeof pattern, "%s20*.txt", stats_folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    int count = 0;
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        snprintf(path, sizeof path, "%s%s", stats_folder, found.cFileName);
        FILE *file = fopen(path, "r");
        int in_list = 0;
        while (file && fgets(line, sizeof line, file) && count < MAX_PRACTICE_ENTRIES) {
            if (!in_list) {
                in_list = !strncmp(line, "Every word", 10);
                continue;
            }
            char word[64];
            double seconds;
            int player, typos;
            if (sscanf(line, "%d\t%63[^\t]\t%lf\t%d", &player, word, &seconds, &typos) != 4 || strlen(word) < 2) continue;
            practice_entry *entry = &entries[count++];
            snprintf(entry->word, sizeof entry->word, "%s", word);
            entry->typos = typos;
            entry->seconds = seconds;
            entry->count = 1;
        }
        if (file) fclose(file);
    } while (FindNextFileA(search, &found) && count < MAX_PRACTICE_ENTRIES);
    FindClose(search);
    qsort(entries, (size_t)count, sizeof *entries, compare_practice_words);
    int merged = 0;
    for (int i = 0; i < count; i++) {
        if (merged && !strcmp(entries[merged - 1].word, entries[i].word)) {
            entries[merged - 1].typos += entries[i].typos;
            entries[merged - 1].seconds += entries[i].seconds;
            entries[merged - 1].count++;
        } else {
            entries[merged++] = entries[i];
        }
    }
    qsort(entries, (size_t)merged, sizeof *entries, compare_practice_need);
    return merged;
}

static void make_practice_pack(HWND dialog) {
    practice_entry *entries = calloc(MAX_PRACTICE_ENTRIES, sizeof *entries);
    if (!entries) return;
    HCURSOR previous_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int count = collect_practice_words(entries);
    char list_path[MAX_PATH], pack_folder[MAX_PATH], error[512], message[768];
    snprintf(list_path, sizeof list_path, "%spractice_misses.txt", stats_folder);
    snprintf(pack_folder, sizeof pack_folder, "%spractice_misses", dict_folder_path);
    FILE *list = fopen(list_path, "w");
    int written = 0, with_typos = 0;
    for (int i = 0; list && i < count && written < PRACTICE_WORDS; i++) {
        fprintf(list, "%s\n", entries[i].word);
        written++;
        with_typos += entries[i].typos > 0;
    }
    if (list) fclose(list);
    free(entries);
    if (written < 10) {
        SetCursor(previous_cursor);
        MessageBoxA(dialog, "Play a few games first: the practice pack is made from the words in your statistics.",
                    "Practice pack", MB_ICONINFORMATION);
        return;
    }
    CreateDirectoryA(dict_folder_path, NULL);
    pack_build_report report;
    int built = build_pack_from_word_list(list_path, pack_folder, GetTickCount(), &report, error, sizeof error);
    SetCursor(previous_cursor);
    if (!built) {
        MessageBoxA(dialog, error, "Practice pack", MB_ICONWARNING);
        return;
    }
    if (practice_pack_ready) practice_pack_ready("practice_misses");
    snprintf(message, sizeof message,
             "Pack \"practice_misses\" made from your %d hardest words (%d of them with typos), gathered from every "
             "game in stats\\.\n\nIt is now at the top of \"In use\" on the Settings tab. Click Save to play with it.",
             written, with_typos);
    MessageBoxA(dialog, message, "Practice pack", MB_ICONINFORMATION);
}

void stats_page_set_practice_callback(void (*callback)(const char *pack_name)) {
    practice_pack_ready = callback;
}

static void refresh_statistics(void) {
    char *text = malloc(MAX_TEXT);
    if (!text) return;
    size_t used = 0;
    text[0] = '\0';
    append_achievements(text, &used);
    append_history(text, &used);
    append_latest_report(text, &used);
    SetWindowTextA(text_box, text);
    free(text);
}

void stats_page_create(HWND dialog, RECT area, const char *game_folder) {
    control_count = 0;
    snprintf(stats_folder, sizeof stats_folder, "%sstats\\", game_folder);
    snprintf(dict_folder_path, sizeof dict_folder_path, "%sdict\\", game_folder);
    int x = area.left, y = area.top, width = area.right - area.left;
    add_control(dialog, WC_STATICW,
                L"Typing statistics of every game (todmod.ini [stats] enabled=1). stats\\practice_words.txt lists "
                L"your hardest words of the last game: turn it into a word pack with \"New pack from word list...\".",
                0, x, y, width, 34, IDC_STATS_LABEL);
    y += 40;
    int text_height = area.bottom - y - BUTTON_HEIGHT - 8;
    text_box = add_control(dialog, WC_EDITW, L"",
                           ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                           x, y, width, text_height, IDC_STATS_TEXT);
    fixed_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(text_box, WM_SETFONT, (WPARAM)fixed_font, TRUE);
    y += text_height + 8;
    add_control(dialog, WC_BUTTONW, L"Open statistics folder", WS_TABSTOP, x, y, BUTTON_WIDTH, BUTTON_HEIGHT,
                IDC_STATS_OPEN_FOLDER);
    add_control(dialog, WC_BUTTONW, L"Refresh", WS_TABSTOP, x + BUTTON_WIDTH + 8, y, 90, BUTTON_HEIGHT, IDC_STATS_REFRESH);
    add_control(dialog, WC_BUTTONW, L"Make a practice pack from my misses", WS_TABSTOP, x + BUTTON_WIDTH + 106, y, 240,
                BUTTON_HEIGHT, IDC_STATS_PRACTICE);
    refresh_statistics();
    stats_page_show(0);
}

void stats_page_show(int visible) {
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
    if (visible) refresh_statistics();
}

int stats_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    if (message != WM_COMMAND) return 0;
    if (LOWORD(wparam) == IDC_STATS_OPEN_FOLDER) {
        CreateDirectoryA(stats_folder, NULL);
        ShellExecuteA(NULL, "open", stats_folder, NULL, NULL, SW_SHOWNORMAL);
        return 1;
    }
    if (LOWORD(wparam) == IDC_STATS_PRACTICE) {
        make_practice_pack(dialog);
        return 1;
    }
    if (LOWORD(wparam) == IDC_STATS_REFRESH) {
        refresh_statistics();
        return 1;
    }
    return 0;
}
