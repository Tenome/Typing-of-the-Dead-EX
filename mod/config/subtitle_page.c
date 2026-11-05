#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "subtitle_page.h"
#include "subtitle_text.h"
#include "subtitle_timing.h"

#define ROW_HEIGHT 24
#define MARGIN 10

enum {
    IDC_SUB_FILE_LABEL = 2100,
    IDC_SUB_FILE,
    IDC_SUB_LOAD,
    IDC_SUB_SAVE,
    IDC_SUB_OPEN,
    IDC_SUB_USE,
    IDC_SUB_LIST,
    IDC_SUB_EDIT_LABEL,
    IDC_SUB_EDIT,
    IDC_SUB_STATUS,
    IDC_SUB_FONT_LABEL,
    IDC_SUB_FONT,
    IDC_SUB_FONT_ALL,
    IDC_SUB_START_LABEL,
    IDC_SUB_START,
    IDC_SUB_END_LABEL,
    IDC_SUB_END,
    IDC_SUB_PLAY,
    IDC_SUB_TIMING_INFO,
    IDC_SUB_COVER_VOICE,
    IDC_SUB_SIZE_LABEL,
    IDC_SUB_SIZE,
    IDC_SUB_POSITION_LABEL,
    IDC_SUB_POSITION,
};

static HWND controls[32];
static int control_count;
static HWND file_box, list_view, edit_box, status_text, use_box, font_box, font_all_box;
static HWND start_box, end_box, timing_info, cover_voice_box, size_box, position_box;
static short record_starts[SUBTITLE_COUNT], record_ends[SUBTITLE_COUNT];
static subtitle_tables tables;
static int tables_ready;
static char game_folder_path[MAX_PATH];
static char subtitles_folder[MAX_PATH];
static char originals[SUBTITLE_COUNT][SUBTITLE_TOD_TEXT + 1];
static char *translations[SUBTITLE_COUNT];
static int selected = -1;
static int updating_edit;

static HWND add_control(HWND dialog, const wchar_t *class_name, const wchar_t *text, DWORD style, DWORD ex_style, int x,
                        int y, int width, int height, int id) {
    HWND control = CreateWindowExW(ex_style, class_name, text, WS_CHILD | style, x, y, width, height, dialog,
                                   (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    controls[control_count++] = control;
    return control;
}

static void utf8_to_wide(const char *text, wchar_t *out, int size) {
    if (!MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, out, size)) out[0] = 0;
}

typedef struct {
    FILE *file;
    IMAGE_SECTION_HEADER sections[16];
    int section_count;
    DWORD image_base;
} exe_file;

static int read_exe(void *context, unsigned va, void *out, unsigned size) {
    exe_file *exe = context;
    DWORD rva = va - exe->image_base;
    for (int i = 0; i < exe->section_count; i++) {
        const IMAGE_SECTION_HEADER *section = &exe->sections[i];
        if (rva < section->VirtualAddress || rva + size > section->VirtualAddress + section->SizeOfRawData) continue;
        return !fseek(exe->file, (long)(rva - section->VirtualAddress + section->PointerToRawData), SEEK_SET) &&
               fread(out, 1, size, exe->file) == size;
    }
    return 0;
}

static void read_timing_tables(const char *game_folder) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sTod.exe", game_folder);
    exe_file exe = {fopen(path, "rb")};
    if (!exe.file) return;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (fread(&dos, sizeof dos, 1, exe.file) == 1 && !fseek(exe.file, dos.e_lfanew, SEEK_SET) &&
        fread(&nt, sizeof nt, 1, exe.file) == 1) {
        exe.image_base = nt.OptionalHeader.ImageBase;
        exe.section_count = nt.FileHeader.NumberOfSections < 16 ? nt.FileHeader.NumberOfSections : 16;
        if (fread(exe.sections, sizeof exe.sections[0], exe.section_count, exe.file) == (size_t)exe.section_count)
            tables_ready = subtitle_tables_read(&tables, SUBTITLE_GAME_COUNT, read_exe, &exe);
    }
    fclose(exe.file);
}

static void voice_path(int line, char *out, size_t size) {
    snprintf(out, size, "%sSOUND\\VOICE\\%s", game_folder_path, tables.lines[line].voice_name);
    for (char *c = out; *c; c++) {
        if (*c == '/') *c = '\\';
    }
}

static void show_timing(void) {
    wchar_t text[200] = L"";
    char start[16] = "", end[16] = "";
    if (selected >= 0 && selected < SUBTITLE_GAME_COUNT) {
        subtitle_format_seconds(record_starts[selected], start, sizeof start);
        subtitle_format_seconds(record_ends[selected], end, sizeof end);
    }
    updating_edit = 1;
    SetWindowTextA(start_box, start);
    SetWindowTextA(end_box, end);
    updating_edit = 0;
    int part = 0, line = tables_ready && selected >= 0 && selected < SUBTITLE_GAME_COUNT ? subtitle_line_of_record(&tables, selected, &part) : -1;
    EnableWindow(start_box, line >= 0);
    EnableWindow(end_box, line >= 0);
    EnableWindow(GetDlgItem(GetParent(start_box), IDC_SUB_PLAY), line >= 0 && subtitle_has_voice(&tables, line));
    if (line >= 0) {
        subtitle_line_timing timing;
        subtitle_game_timing(&tables, line, &timing);
        char voice[MAX_PATH];
        int voice_frames = 0;
        if (subtitle_has_voice(&tables, line)) {
            voice_path(line, voice, sizeof voice);
            voice_frames = subtitle_wave_frames(voice);
        }
        swprintf(text, 200, L"Game: %.2f-%.2f s (part %d of %d). Voice %hs: %.2f s.", timing.start[part] / 60.0,
                 timing.end[part] / 60.0, part + 1, tables.lines[line].part_count,
                 voice_frames ? tables.lines[line].voice_name : "none", voice_frames / 60.0);
    } else if (selected >= 0) {
        lstrcpyW(text, L"This line has no timing of its own.");
    }
    SetWindowTextW(timing_info, text);
}

static void timing_changed(HWND box) {
    if (updating_edit || selected < 0 || selected >= SUBTITLE_GAME_COUNT) return;
    char value[32];
    GetWindowTextA(box, value, sizeof value);
    short frames = (short)subtitle_parse_seconds(value);
    if (box == start_box) record_starts[selected] = frames;
    else record_ends[selected] = frames;
}

static void play_voice(void) {
    int line = tables_ready && selected >= 0 && selected < SUBTITLE_GAME_COUNT ? subtitle_line_of_record(&tables, selected, NULL) : -1;
    if (line < 0 || !subtitle_has_voice(&tables, line)) return;
    char path[MAX_PATH];
    voice_path(line, path, sizeof path);
    PlaySoundA(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

static int read_original_subtitles(const char *game_folder) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sTod.exe", game_folder);
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    int ok = fread(&dos, sizeof dos, 1, file) == 1 && !fseek(file, dos.e_lfanew, SEEK_SET) && fread(&nt, sizeof nt, 1, file) == 1;
    long text_offset = -1;
    for (int i = 0; ok && i < nt.FileHeader.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER section;
        if (fread(&section, sizeof section, 1, file) != 1) break;
        DWORD rva = SUBTITLE_TOD_VA - nt.OptionalHeader.ImageBase;
        if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.SizeOfRawData) {
            text_offset = (long)(rva - section.VirtualAddress + section.PointerToRawData);
        }
    }
    for (int i = 0; i < SUBTITLE_INTRO_COUNT; i++) {
        snprintf(originals[SUBTITLE_INTRO_FIRST + i], SUBTITLE_TOD_TEXT + 1, "[Attract intro %d]", i + 1);
    }
    for (int i = 0; text_offset >= 0 && i < SUBTITLE_GAME_COUNT; i++) {
        fseek(file, text_offset + i * SUBTITLE_TOD_STRIDE, SEEK_SET);
        if (fread(originals[i], 1, SUBTITLE_TOD_TEXT, file) != SUBTITLE_TOD_TEXT) break;
        originals[i][SUBTITLE_TOD_TEXT] = '\0';
    }
    fclose(file);
    return text_offset >= 0;
}

static void set_translation(int index, const char *text) {
    free(translations[index]);
    translations[index] = text && *text ? _strdup(text) : NULL;
}

static int row_of_index(int index) {
    LVFINDINFOW find = {LVFI_PARAM};
    find.lParam = index;
    return (int)SendMessageW(list_view, LVM_FINDITEMW, (WPARAM)-1, (LPARAM)&find);
}

static void show_translation_in_list(int index) {
    int row = row_of_index(index);
    if (row < 0) return;
    wchar_t wide[SUBTITLE_MAX_TEXT];
    utf8_to_wide(translations[index], wide, SUBTITLE_MAX_TEXT);
    LVITEMW item = {0};
    item.iSubItem = 2;
    item.pszText = wide;
    SendMessageW(list_view, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&item);
}

static void fill_list(void) {
    SendMessageW(list_view, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (int i = 0; i < SUBTITLE_COUNT; i++) {
        if (!originals[i][0]) continue;
        wchar_t number[16], original[SUBTITLE_TOD_TEXT + 1];
        swprintf(number, 16, L"%d", i);
        MultiByteToWideChar(CP_ACP, 0, originals[i], -1, original, SUBTITLE_TOD_TEXT + 1);
        LVITEMW item = {LVIF_TEXT | LVIF_PARAM};
        item.iItem = row++;
        item.pszText = number;
        item.lParam = i;
        int at = (int)SendMessageW(list_view, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.mask = LVIF_TEXT;
        item.iItem = at;
        item.iSubItem = 1;
        item.pszText = original;
        SendMessageW(list_view, LVM_SETITEMTEXTW, (WPARAM)at, (LPARAM)&item);
        show_translation_in_list(i);
    }
}

static void update_status(void) {
    if (selected < 0) {
        SetWindowTextW(status_text, L"Select a line. Use | for a second line. The game font has A-Z a-z 0-9 and ! ' , - . ? ~ only; "
                                    L"accents are simplified (é -> e, ß -> ss, : -> -); lines in other scripts (Japanese, Cyrillic, ...) use a Windows font.");
        return;
    }
    const char *text = translations[selected] ? translations[selected] : originals[selected];
    if (SendMessageW(font_all_box, BM_GETCHECK, 0, 0) == BST_CHECKED || subtitle_needs_windows_font(text)) {
        wchar_t wide[SUBTITLE_MAX_TEXT + 80] = L"Shown in game with a Windows font: ";
        size_t prefix = wcslen(wide);
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide + prefix, (int)(SUBTITLE_MAX_TEXT + 80 - prefix));
        SetWindowTextW(status_text, wide);
        return;
    }
    char shown[SUBTITLE_MAX_TEXT];
    int substituted = subtitle_to_game_text(text, shown, sizeof shown);
    int longest = 0, current = 0;
    for (char *c = shown;; c++) {
        if (*c == '\n' || !*c) {
            if (current > longest) longest = current;
            current = 0;
            if (!*c) break;
        } else {
            current++;
        }
    }
    for (char *c = shown; *c; c++) if (*c == '\n') *c = '|';
    char message[SUBTITLE_MAX_TEXT + 160];
    snprintf(message, sizeof message, "Shown in game as: %s%s%s", shown,
             substituted ? "   (some characters simplified)" : "",
             longest > SUBTITLE_MAX_LINE ? "   (line too long: 57 characters fit)" : "");
    wchar_t wide[SUBTITLE_MAX_TEXT + 160];
    MultiByteToWideChar(CP_ACP, 0, message, -1, wide, SUBTITLE_MAX_TEXT + 160);
    SetWindowTextW(status_text, wide);
}

static void select_row(void) {
    int row = (int)SendMessageW(list_view, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (row < 0) return;
    LVITEMW item = {LVIF_PARAM};
    item.iItem = row;
    SendMessageW(list_view, LVM_GETITEMW, 0, (LPARAM)&item);
    selected = (int)item.lParam;
    wchar_t wide[SUBTITLE_MAX_TEXT];
    utf8_to_wide(translations[selected], wide, SUBTITLE_MAX_TEXT);
    updating_edit = 1;
    SetWindowTextW(edit_box, wide);
    updating_edit = 0;
    update_status();
    show_timing();
}

static void edit_changed(void) {
    if (updating_edit || selected < 0) return;
    wchar_t wide[SUBTITLE_MAX_TEXT];
    char text[SUBTITLE_MAX_TEXT * 3];
    GetWindowTextW(edit_box, wide, SUBTITLE_MAX_TEXT);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof text, NULL, NULL);
    set_translation(selected, text);
    show_translation_in_list(selected);
    update_status();
}

static void file_name(char *name, size_t size) {
    wchar_t wide[MAX_PATH];
    GetWindowTextW(file_box, wide, MAX_PATH);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, (int)size, NULL, NULL);
    char *extension = strrchr(name, '.');
    if (extension && !_stricmp(extension, ".tsv")) *extension = '\0';
}

static void load_file(HWND dialog) {
    char name[MAX_PATH], path[MAX_PATH], line[4096];
    file_name(name, sizeof name);
    snprintf(path, sizeof path, "%s%s.tsv", subtitles_folder, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        MessageBoxA(dialog, "No such subtitle file yet. Edit lines and press Save to create it.", "Subtitles", MB_ICONINFORMATION);
        return;
    }
    for (int i = 0; i < SUBTITLE_COUNT; i++) {
        set_translation(i, NULL);
        record_starts[i] = record_ends[i] = SUBTITLE_NO_TIME;
    }
    while (fgets(line, sizeof line, file)) {
        char *cursor = line;
        if ((unsigned char)cursor[0] == 0xEF && (unsigned char)cursor[1] == 0xBB && (unsigned char)cursor[2] == 0xBF) cursor += 3;
        if (*cursor < '0' || *cursor > '9') continue;
        int index = atoi(cursor);
        char *fields[4] = {NULL, NULL, NULL, NULL};
        int count = 0;
        for (char *tab = strchr(cursor, '\t'); tab && count < 4; tab = strchr(tab + 1, '\t')) fields[count++] = tab + 1;
        char *text = count >= 2 ? fields[1] : count == 1 ? fields[0] : NULL;
        if (!text || index < 0 || index >= SUBTITLE_COUNT) continue;
        if (count >= 3) record_starts[index] = (short)subtitle_parse_seconds(fields[2]);
        if (count >= 4) record_ends[index] = (short)subtitle_parse_seconds(fields[3]);
        text[strcspn(text, "\t\r\n")] = '\0';
        set_translation(index, text);
    }
    fclose(file);
    fill_list();
    selected = -1;
    SetWindowTextW(edit_box, L"");
    update_status();
}

static void save_file(HWND dialog) {
    char name[MAX_PATH], path[MAX_PATH];
    file_name(name, sizeof name);
    if (!name[0]) {
        MessageBoxA(dialog, "Please type a file name first.", "Subtitles", MB_ICONWARNING);
        return;
    }
    CreateDirectoryA(subtitles_folder, NULL);
    snprintf(path, sizeof path, "%s%s.tsv", subtitles_folder, name);
    FILE *file = fopen(path, "wb");
    if (!file) {
        MessageBoxA(dialog, "Could not write the subtitle file.", "Subtitles", MB_ICONERROR);
        return;
    }
    fputs("# Typing of the Dead EX subtitles. Columns: line number, original, your text (UTF-8), start, end.\n"
          "# Empty text keeps the original. | starts a second line. start/end: seconds after the voice starts\n"
          "# (empty = the game's timing).\n", file);
    for (int i = 0; i < SUBTITLE_COUNT; i++) {
        if (!originals[i][0]) continue;
        fprintf(file, "%d\t%s\t%s", i, originals[i], translations[i] ? translations[i] : "");
        if (record_starts[i] != SUBTITLE_NO_TIME || record_ends[i] != SUBTITLE_NO_TIME) {
            char start[16], end[16];
            subtitle_format_seconds(record_starts[i], start, sizeof start);
            subtitle_format_seconds(record_ends[i], end, sizeof end);
            fprintf(file, "\t%s\t%s", start, end);
        }
        fputc('\n', file);
    }
    fclose(file);
    wchar_t wide[MAX_PATH];
    GetWindowTextW(file_box, wide, MAX_PATH);
    if (SendMessageW(file_box, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wide) == CB_ERR) {
        SendMessageW(file_box, CB_ADDSTRING, 0, (LPARAM)wide);
    }
    MessageBoxA(dialog, "Saved. Tick \"Use this file in game\" and press Save below to use it.", "Subtitles", MB_ICONINFORMATION);
}

static void fill_file_list(const char *current) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s*.tsv", subtitles_folder);
    WIN32_FIND_DATAW found;
    wchar_t wide_pattern[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, pattern, -1, wide_pattern, MAX_PATH);
    HANDLE search = FindFirstFileW(wide_pattern, &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *extension = wcsrchr(found.cFileName, L'.');
            if (extension) *extension = 0;
            SendMessageW(file_box, CB_ADDSTRING, 0, (LPARAM)found.cFileName);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    wchar_t wide[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, current[0] ? current : "my_subtitles", -1, wide, MAX_PATH);
    SetWindowTextW(file_box, wide);
}

static int CALLBACK add_subtitle_font(const LOGFONTW *font, const TEXTMETRICW *metrics, DWORD type, LPARAM unused) {
    (void)metrics;
    (void)type;
    (void)unused;
    if (font->lfFaceName[0] == L'@') return 1;
    if (SendMessageW(font_box, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)font->lfFaceName) == CB_ERR) {
        SendMessageW(font_box, CB_ADDSTRING, 0, (LPARAM)font->lfFaceName);
    }
    return 1;
}

static void fill_subtitle_fonts(HWND dialog) {
    LOGFONTW query = {0};
    query.lfCharSet = DEFAULT_CHARSET;
    HDC dc = GetDC(dialog);
    EnumFontFamiliesExW(dc, &query, add_subtitle_font, 0, 0);
    ReleaseDC(dialog, dc);
}

static void listed_font_name(wchar_t *face) {
    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, face);
    HDC dc = GetDC(font_box);
    HGDIOBJ previous = SelectObject(dc, font);
    GetTextFaceW(dc, LF_FACESIZE, face);
    SelectObject(dc, previous);
    ReleaseDC(font_box, dc);
    DeleteObject(font);
}

void subtitle_page_set_font(const char *face, int all_lines) {
    wchar_t wide[LF_FACESIZE];
    MultiByteToWideChar(CP_ACP, 0, face[0] ? face : "Meiryo", -1, wide, LF_FACESIZE);
    LRESULT index = SendMessageW(font_box, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wide);
    if (index == CB_ERR) {
        listed_font_name(wide);
        index = SendMessageW(font_box, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wide);
    }
    SendMessageW(font_box, CB_SETCURSEL, index == CB_ERR ? 0 : index, 0);
    SendMessageW(font_all_box, BM_SETCHECK, all_lines ? BST_CHECKED : BST_UNCHECKED, 0);
    update_status();
}

void subtitle_page_font(char *face, size_t size, int *all_lines) {
    wchar_t wide[LF_FACESIZE] = L"";
    GetWindowTextW(font_box, wide, LF_FACESIZE);
    WideCharToMultiByte(CP_ACP, 0, wide, -1, face, (int)size, NULL, NULL);
    *all_lines = SendMessageW(font_all_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void subtitle_page_create(HWND dialog, const char *game_folder, RECT area, const char *current_file) {
    control_count = 0;
    snprintf(subtitles_folder, sizeof subtitles_folder, "%ssubtitles\\", game_folder);
    int x = area.left, y = area.top, width = area.right - area.left;
    add_control(dialog, WC_STATICW, L"Subtitle file:", 0, 0, x, y + 4, 70, 18, IDC_SUB_FILE_LABEL);
    file_box = add_control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 0, x + 74, y,
                           200, 200, IDC_SUB_FILE);
    add_control(dialog, WC_BUTTONW, L"Load", WS_TABSTOP, 0, x + 282, y, 70, ROW_HEIGHT, IDC_SUB_LOAD);
    add_control(dialog, WC_BUTTONW, L"Save file", WS_TABSTOP, 0, x + 358, y, 80, ROW_HEIGHT, IDC_SUB_SAVE);
    add_control(dialog, WC_BUTTONW, L"Open folder", WS_TABSTOP, 0, x + 444, y, 90, ROW_HEIGHT, IDC_SUB_OPEN);
    y += ROW_HEIGHT + 6;
    use_box = add_control(dialog, WC_BUTTONW, L"Use this file in game (shown at the bottom; press Save below)",
                          BS_AUTOCHECKBOX | WS_TABSTOP, 0, x, y, width, 20, IDC_SUB_USE);
    SendMessageW(use_box, BM_SETCHECK, current_file[0] ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    add_control(dialog, WC_STATICW, L"Font:", 0, 0, x, y + 4, 70, 18, IDC_SUB_FONT_LABEL);
    font_box = add_control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | CBS_SORT | WS_VSCROLL | WS_TABSTOP, 0, x + 74, y,
                           200, 300, IDC_SUB_FONT);
    fill_subtitle_fonts(dialog);
    font_all_box = add_control(dialog, WC_BUTTONW, L"Use it for all subtitles (otherwise only for Japanese and other scripts)",
                               BS_AUTOCHECKBOX | WS_TABSTOP, 0, x + 282, y + 2, width - 282, 20, IDC_SUB_FONT_ALL);
    y += ROW_HEIGHT + 6;
    int bottom_rows = 22 + ROW_HEIGHT + 6 + 40 + ROW_HEIGHT + 30 + ROW_HEIGHT + 4;
    list_view = add_control(dialog, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                            WS_EX_CLIENTEDGE, x, y, width, area.bottom - y - bottom_rows, IDC_SUB_LIST);
    SendMessageW(list_view, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LVCOLUMNW column = {LVCF_TEXT | LVCF_WIDTH};
    column.cx = 44;
    column.pszText = L"#";
    SendMessageW(list_view, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
    column.cx = (width - 70) / 2;
    column.pszText = L"Original (English)";
    SendMessageW(list_view, LVM_INSERTCOLUMNW, 1, (LPARAM)&column);
    column.pszText = L"Your text";
    SendMessageW(list_view, LVM_INSERTCOLUMNW, 2, (LPARAM)&column);
    y = area.bottom - bottom_rows + 6;
    add_control(dialog, WC_STATICW, L"Your text for the selected line:", 0, 0, x, y, width, 16, IDC_SUB_EDIT_LABEL);
    y += 18;
    edit_box = add_control(dialog, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, x, y, width, ROW_HEIGHT,
                           IDC_SUB_EDIT);
    y += ROW_HEIGHT + 4;
    status_text = add_control(dialog, WC_STATICW, L"", 0, 0, x, y, width, 36, IDC_SUB_STATUS);
    y += 40;
    add_control(dialog, WC_STATICW, L"Start (s):", 0, 0, x, y + 4, 54, 18, IDC_SUB_START_LABEL);
    start_box = add_control(dialog, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, x + 56, y, 56, ROW_HEIGHT,
                            IDC_SUB_START);
    add_control(dialog, WC_STATICW, L"End (s):", 0, 0, x + 122, y + 4, 48, 18, IDC_SUB_END_LABEL);
    end_box = add_control(dialog, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, x + 172, y, 56, ROW_HEIGHT,
                          IDC_SUB_END);
    add_control(dialog, WC_BUTTONW, L"Play voice", WS_TABSTOP, 0, x + 238, y, 84, ROW_HEIGHT, IDC_SUB_PLAY);
    timing_info = add_control(dialog, WC_STATICW, L"", 0, 0, x + 330, y + 4, width - 330, 18, IDC_SUB_TIMING_INFO);
    y += ROW_HEIGHT + 4;
    cover_voice_box = add_control(dialog, WC_BUTTONW,
                                  L"Keep each subtitle up until its voice line ends (start/end: seconds after the voice starts; empty = game timing)",
                                  BS_AUTOCHECKBOX | WS_TABSTOP, 0, x, y, width, 20, IDC_SUB_COVER_VOICE);
    y += 24;
    add_control(dialog, WC_STATICW, L"Text size (%, Windows-font lines):", 0, 0, x, y + 4, 190, 18, IDC_SUB_SIZE_LABEL);
    size_box = add_control(dialog, WC_EDITW, L"100", ES_NUMBER | WS_TABSTOP, WS_EX_CLIENTEDGE, x + 192, y, 50, ROW_HEIGHT,
                           IDC_SUB_SIZE);
    add_control(dialog, WC_STATICW, L"Height on screen (% from the top, empty = game's 82):", 0, 0, x + 262, y + 4, 300, 18,
                IDC_SUB_POSITION_LABEL);
    position_box = add_control(dialog, WC_EDITW, L"", ES_NUMBER | WS_TABSTOP, WS_EX_CLIENTEDGE, x + 566, y, 50, ROW_HEIGHT,
                               IDC_SUB_POSITION);
    snprintf(game_folder_path, sizeof game_folder_path, "%s", game_folder);
    for (int i = 0; i < SUBTITLE_COUNT; i++) record_starts[i] = record_ends[i] = SUBTITLE_NO_TIME;
    read_timing_tables(game_folder);
    read_original_subtitles(game_folder);
    fill_file_list(current_file);
    if (current_file[0]) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s%s.tsv", subtitles_folder, current_file);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) load_file(dialog);
    }
    fill_list();
    update_status();
    subtitle_page_show(0);
}

void subtitle_page_show(int visible) {
    for (int i = 0; i < control_count; i++) ShowWindow(controls[i], visible ? SW_SHOW : SW_HIDE);
}

void subtitle_page_chosen_file(char *out, size_t size) {
    out[0] = '\0';
    if (SendMessageW(use_box, BM_GETCHECK, 0, 0) == BST_CHECKED) file_name(out, size);
}

int subtitle_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NOTIFY && ((NMHDR *)lparam)->idFrom == IDC_SUB_LIST &&
        ((NMHDR *)lparam)->code == LVN_ITEMCHANGED) {
        const NMLISTVIEW *change = (const NMLISTVIEW *)lparam;
        if (change->uNewState & LVIS_SELECTED) select_row();
        return 1;
    }
    if (message != WM_COMMAND) return 0;
    switch (LOWORD(wparam)) {
    case IDC_SUB_FONT_ALL:
        update_status();
        return 1;
    case IDC_SUB_EDIT:
        if (HIWORD(wparam) == EN_CHANGE) edit_changed();
        return 1;
    case IDC_SUB_LOAD:
        load_file(dialog);
        return 1;
    case IDC_SUB_START:
    case IDC_SUB_END:
        if (HIWORD(wparam) == EN_CHANGE) timing_changed((HWND)lparam);
        return 1;
    case IDC_SUB_PLAY:
        play_voice();
        return 1;
    case IDC_SUB_SAVE:
        save_file(dialog);
        return 1;
    case IDC_SUB_OPEN:
        CreateDirectoryA(subtitles_folder, NULL);
        ShellExecuteA(NULL, "open", subtitles_folder, NULL, NULL, SW_SHOWNORMAL);
        return 1;
    }
    return 0;
}

void subtitle_page_set_cover_voice(int on) {
    SendMessageW(cover_voice_box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

int subtitle_page_cover_voice(void) {
    return SendMessageW(cover_voice_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void subtitle_page_set_layout(int size_percent, int position_percent) {
    char text[16];
    snprintf(text, sizeof text, "%d", size_percent >= 50 && size_percent <= 300 ? size_percent : 100);
    SetWindowTextA(size_box, text);
    if (position_percent >= 10 && position_percent <= 98) snprintf(text, sizeof text, "%d", position_percent);
    else text[0] = '\0';
    SetWindowTextA(position_box, text);
}

void subtitle_page_layout(int *size_percent, int *position_percent) {
    char text[16];
    GetWindowTextA(size_box, text, sizeof text);
    *size_percent = atoi(text);
    if (*size_percent < 50 || *size_percent > 300) *size_percent = 100;
    GetWindowTextA(position_box, text, sizeof text);
    *position_percent = atoi(text);
    if (*position_percent < 10 || *position_percent > 98) *position_percent = 0;
}
