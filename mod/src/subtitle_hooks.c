#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "subtitle_hooks.h"
#include "subtitle_text.h"
#include "subtitle_timing.h"
#include "voice_packs.h"
#include "log.h"
#include "MinHook.h"

#define DRAW_SUBTITLE_VA 0x00425140
#define SUBTITLE_RECORDS 0x0051eea8
#define SUBTITLE_RECORD_SIZE 0x40
#define SUBTITLE_TEXT_OFFSET 4
#define SUBTITLE_TASK_VA 0x00425f80
#define GAME_LANGUAGE (*(volatile short *)0x00a1b6b2)
#define LANGUAGE_JAPANESE 1
#define SCENE_ENDING (*(volatile int *)0x009f0adc)
#define CUTSCENE_SKIPPED (*(volatile int *)0x00a3e7a4)
#define TASK_SUBTITLE_ID 0x34
#define TASK_TIMER 0x36
#define TASK_STEP 0x38
#define LINE_HEIGHT 22.0f
#define SCREEN_2D_HEIGHT 480.0f
#define SUBTITLE_TOP_2D 392.0f
#define FONT_HEIGHT_2D 22.0f
#define DEVICE_GET_RENDER_TARGET 9
#define SURFACE_RELEASE 2
#define SURFACE_GET_DC 17
#define SURFACE_GET_DESC 22
#define SURFACE_RELEASE_DC 26
#define SURFACE_DESC_BYTES 124

typedef void(__cdecl *draw_subtitle_fn)(float x, float y, const char *text);

typedef int(__cdecl *vsync_fn)(void);
typedef void(__cdecl *subtitle_task_fn)(unsigned char *task);
typedef HRESULT(__stdcall *com_call_fn)(void *self, void *argument);
typedef ULONG(__stdcall *com_release_fn)(void *self);

static draw_subtitle_fn original_draw_subtitle;
static vsync_fn original_vsync;
static subtitle_task_fn original_subtitle_task;
static int text_pass_for_japanese, line_missing, current_subtitle_id = -1;
static char *replacements[SUBTITLE_COUNT];
static wchar_t *windows_font_lines[SUBTITLE_COUNT];
static short record_starts[SUBTITLE_COUNT], record_ends[SUBTITLE_COUNT];
static int timed_records, text_lines_loaded;
static subtitle_tables timing_tables;
static subtitle_line_timing line_timings[SUBTITLE_LINE_IDS];
static unsigned char line_retimed[SUBTITLE_LINE_IDS];
static int windows_font_line_count, windows_font_everywhere;
static wchar_t game_line[SUBTITLE_MAX_TEXT];
static const wchar_t *requested_line, *shown_line;
static void ***direct3d_device;
static volatile int *frame_skip_count;
static char font_face[LF_FACESIZE] = "Meiryo";
static HFONT font;
static int font_height;
static float subtitle_scale = 1.0f, subtitle_top_2d = SUBTITLE_TOP_2D;

static void *com_method(void *object, int slot) {
    return (*(void ***)object)[slot];
}

static void draw_outlined_lines(HDC dc, int width, int height, const wchar_t *text) {
    int em = (int)(FONT_HEIGHT_2D * subtitle_scale * (float)height / SCREEN_2D_HEIGHT + 0.5f);
    if (!font || font_height != em) {
        if (font) DeleteObject(font);
        font = CreateFontA(-em, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, font_face);
        font_height = em;
    }
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    int lines = 1;
    for (const wchar_t *c = text; *c; c++) lines += *c == L'|';
    int line_step = (int)(LINE_HEIGHT * subtitle_scale * (float)height / SCREEN_2D_HEIGHT + 0.5f);
    if (line_step < em + 2) line_step = em + 2;
    int top = (int)(subtitle_top_2d * (float)height / SCREEN_2D_HEIGHT) - (lines - 1) * line_step;
    int outline = height >= 720 ? 2 : 1;
    const wchar_t *start = text;
    for (int line = 0; line < lines; line++) {
        const wchar_t *end = wcschr(start, L'|');
        int length = end ? (int)(end - start) : (int)wcslen(start);
        SIZE size;
        GetTextExtentPoint32W(dc, start, length, &size);
        int x = (width - size.cx) / 2, y = top + line * line_step;
        SetTextColor(dc, RGB(0, 0, 0));
        for (int dy = -outline; dy <= outline; dy++) {
            for (int dx = -outline; dx <= outline; dx++) {
                if (dx || dy) TextOutW(dc, x + dx, y + dy, start, length);
            }
        }
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, x, y, start, length);
        if (!end) break;
        start = end + 1;
    }
    SelectObject(dc, previous);
}

#define MAX_OVERLAYS 4

static overlay_draw_fn overlays[MAX_OVERLAYS];
static int overlay_count, vsync_hooked;

static void draw_subtitle_line(HDC dc, int width, int height) {
    if (shown_line) draw_outlined_lines(dc, width, height, shown_line);
}

void subtitle_draw_overlay(void) {
    if (!direct3d_device || !*direct3d_device) return;
    int wanted = shown_line != NULL;
    for (int i = 0; i < overlay_count && !wanted; i++) wanted = overlays[i](NULL, 0, 0);
    if (!wanted) return;
    void *surface = NULL;
    if (FAILED(((com_call_fn)com_method(*direct3d_device, DEVICE_GET_RENDER_TARGET))(*direct3d_device, &surface)) || !surface) return;
    DWORD desc[SURFACE_DESC_BYTES / 4] = {SURFACE_DESC_BYTES};
    HDC dc = NULL;
    if (SUCCEEDED(((com_call_fn)com_method(surface, SURFACE_GET_DESC))(surface, desc)) &&
        SUCCEEDED(((com_call_fn)com_method(surface, SURFACE_GET_DC))(surface, &dc))) {
        draw_subtitle_line(dc, (int)desc[3], (int)desc[2]);
        for (int i = 0; i < overlay_count; i++) overlays[i](dc, (int)desc[3], (int)desc[2]);
        ((com_call_fn)com_method(surface, SURFACE_RELEASE_DC))(surface, dc);
    }
    ((com_release_fn)com_method(surface, SURFACE_RELEASE))(surface);
}

static int __cdecl vsync_hook(void) {
    if (!frame_skip_count || !*frame_skip_count) {
        shown_line = requested_line;
        requested_line = NULL;
        subtitle_draw_overlay();
    }
    return original_vsync();
}

static const char *hook_vsync(void) {
    if (vsync_hooked) return "already hooked";
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    if (!hglcore) return "no HglCore";
    direct3d_device = (void ***)GetProcAddress(hglcore, "g_lpD3DDevice");
    frame_skip_count = (volatile int *)GetProcAddress(hglcore, "hlDelayCount");
    void *vsync = (void *)GetProcAddress(hglcore, "VSync");
    MH_STATUS status = vsync && direct3d_device ? MH_CreateHook(vsync, vsync_hook, (void **)&original_vsync)
                                                : MH_ERROR_FUNCTION_NOT_FOUND;
    if (status == MH_OK) status = MH_EnableHook(vsync);
    vsync_hooked = status == MH_OK;
    return MH_StatusToString(status);
}

void overlay_add(overlay_draw_fn draw) {
    if (overlay_count < MAX_OVERLAYS) overlays[overlay_count++] = draw;
    hook_vsync();
}

static void __cdecl draw_subtitle_hook(float x, float y, const char *text) {
    uintptr_t offset = (uintptr_t)text - (SUBTITLE_RECORDS + SUBTITLE_TEXT_OFFSET);
    int index = (int)(offset / SUBTITLE_RECORD_SIZE);
    int is_record = (uintptr_t)text >= SUBTITLE_RECORDS && offset % SUBTITLE_RECORD_SIZE == 0 && index < SUBTITLE_COUNT;
    int intro_line = current_subtitle_id - SUBTITLE_INTRO_FIRST_ID;
    if (is_record && index == SUBTITLE_INTRO_EMPTY_RECORD && intro_line >= 0 && intro_line < SUBTITLE_INTRO_COUNT) {
        index = SUBTITLE_INTRO_FIRST + intro_line;
    }
    if (!is_record || !replacements[index]) {
        if (text_pass_for_japanese) {
            line_missing = 1;
            return;
        }
        if (is_record && windows_font_everywhere) {
            MultiByteToWideChar(CP_ACP, 0, text, -1, game_line, SUBTITLE_MAX_TEXT);
            requested_line = game_line;
            return;
        }
        original_draw_subtitle(x, y + subtitle_top_2d - SUBTITLE_TOP_2D, text);
        return;
    }
    if (windows_font_lines[index]) {
        requested_line = windows_font_lines[index];
        return;
    }
    int lines = 1;
    for (const char *c = replacements[index]; *c; c++) lines += *c == '\n';
    char line[SUBTITLE_MAX_LINE + 1];
    const char *start = replacements[index];
    for (int i = 0; i < lines; i++) {
        const char *end = strchr(start, '\n');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        if (length > SUBTITLE_MAX_LINE) length = SUBTITLE_MAX_LINE;
        memcpy(line, start, length);
        line[length] = '\0';
        original_draw_subtitle(x, y + subtitle_top_2d - SUBTITLE_TOP_2D - (float)(lines - 1 - i) * LINE_HEIGHT, line);
        start = end ? end + 1 : start + length;
    }
}

static void install_windows_font_overlay(const char *ini_path) {
    GetPrivateProfileStringA("text", "subtitle_font", font_face, font_face, sizeof font_face, ini_path);
    const char *status = hook_vsync();
    log_line("subtitles: %s drawn with Windows font '%s' (hook VSync: %s)",
             windows_font_everywhere ? "all lines" : "lines the game font lacks", font_face, status);
}

static void run_subtitle_task(unsigned char *task) {
    current_subtitle_id = *(short *)(task + TASK_SUBTITLE_ID);
    if (GAME_LANGUAGE != LANGUAGE_JAPANESE || !text_lines_loaded) {
        original_subtitle_task(task);
        current_subtitle_id = -1;
        return;
    }
    short timer = *(short *)(task + TASK_TIMER), step = *(short *)(task + TASK_STEP);
    line_missing = 0;
    text_pass_for_japanese = 1;
    GAME_LANGUAGE = 0;
    original_subtitle_task(task);
    GAME_LANGUAGE = LANGUAGE_JAPANESE;
    text_pass_for_japanese = 0;
    if (!line_missing || timer == 1 || SCENE_ENDING || CUTSCENE_SKIPPED) {
        current_subtitle_id = -1;
        return;
    }
    *(short *)(task + TASK_TIMER) = timer;
    *(short *)(task + TASK_STEP) = step;
    original_subtitle_task(task);
    current_subtitle_id = -1;
}

static int timing_shows_text(const subtitle_line_timing *timing, int elapsed) {
    for (int part = 0; part < SUBTITLE_MAX_PARTS; part++) {
        if (timing->start[part] != SUBTITLE_NO_TIME && elapsed >= timing->start[part] && elapsed < timing->end[part]) return 1;
    }
    return 0;
}

static void __cdecl subtitle_task_hook(unsigned char *task) {
    short line = *(short *)(task + TASK_SUBTITLE_ID);
    if (line < 0 || line >= SUBTITLE_LINE_IDS || !line_retimed[line]) {
        run_subtitle_task(task);
        return;
    }
    const subtitle_line_timing *timing = &line_timings[line];
    short timer = *(short *)(task + TASK_TIMER);
    int elapsed = timing->total - timer;
    int shows = timing_shows_text(timing, elapsed);
    static int logged_events, last_line = -1, last_shows = -1, last_step = -1;
    short shown_step = *(short *)(task + TASK_STEP);
    if (logged_events < 60 && (line != last_line || shows != last_shows || shown_step != last_step)) {
        log_line("subtitles: line %d %s (part %d) at frame %d of %d", line, shows ? "shown" : "hidden", shown_step + 1,
                 elapsed, timing->total);
        logged_events++;
    }
    last_line = line;
    last_shows = shows;
    last_step = shown_step;
    if (!shows && timer > 1 && !SCENE_ENDING && !CUTSCENE_SKIPPED) {
        *(short *)(task + TASK_TIMER) = (short)(timer - 1);
        return;
    }
    const subtitle_line *entry = &timing_tables.lines[line];
    short step = *(short *)(task + TASK_STEP);
    short *switch_value = NULL, saved = 0;
    if (step >= 0 && step < entry->part_count) {
        switch_value = (short *)(SUBTITLE_RECORDS_VA + entry->parts[step] * SUBTITLE_RECORD_STRIDE + SUBTITLE_RECORD_SWITCH);
        saved = *switch_value;
        *switch_value = step + 1 < entry->part_count ? (short)(timing->total - timing->start[step + 1]) : 0;
    }
    run_subtitle_task(task);
    if (switch_value) *switch_value = saved;
}

static void set_record_time(short *times, int record, const char *field) {
    if (!field || record < 0 || record >= SUBTITLE_GAME_COUNT) return;
    char value[32];
    lstrcpynA(value, field, sizeof value);
    value[strcspn(value, "\t\r\n")] = '\0';
    times[record] = (short)subtitle_parse_seconds(value);
}

static int load_subtitles(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    char line[2048];
    int loaded = 0;
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
        if (count >= 3) {
            set_record_time(record_starts, index, fields[2]);
            set_record_time(record_ends, index, count >= 4 ? fields[3] : NULL);
            if (record_starts[index] != SUBTITLE_NO_TIME || record_ends[index] != SUBTITLE_NO_TIME) timed_records++;
        }
        text[strcspn(text, "\t\r\n")] = '\0';
        if (!*text) continue;
        char converted[SUBTITLE_MAX_TEXT];
        subtitle_to_game_text(text, converted, sizeof converted);
        free(windows_font_lines[index]);
        windows_font_lines[index] = NULL;
        if (windows_font_everywhere || subtitle_needs_windows_font(text)) {
            int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
            windows_font_lines[index] = malloc(length * sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, 0, text, -1, windows_font_lines[index], length);
            windows_font_line_count++;
        }
        free(replacements[index]);
        replacements[index] = _strdup(converted);
        loaded++;
    }
    fclose(file);
    return loaded;
}

static int read_game_memory(void *context, unsigned va, void *out, unsigned size) {
    (void)context;
    if (IsBadReadPtr((const void *)(uintptr_t)va, size)) return 0;
    memcpy(out, (const void *)(uintptr_t)va, size);
    return 1;
}

static int voice_length_frames(const subtitle_line *entry) {
    char game_path[MAX_PATH];
    snprintf(game_path, sizeof game_path, "Sound/Voice/%s", entry->voice_name);
    return subtitle_wave_frames(voice_packs_redirect(game_path));
}

static void make_writable(void *address, size_t size) {
    DWORD protection;
    VirtualProtect(address, size, PAGE_READWRITE, &protection);
}

static int retime_lines(int cover_voice) {
    if (!subtitle_tables_read(&timing_tables, SUBTITLE_GAME_COUNT, read_game_memory, NULL)) {
        log_line("subtitles: timing tables not readable");
        return 0;
    }
    int retimed = 0, extended = 0;
    make_writable((void *)SUBTITLE_LINE_TABLE_VA, SUBTITLE_LINE_IDS * SUBTITLE_LINE_STRIDE);
    make_writable((void *)SUBTITLE_RECORDS_VA, SUBTITLE_GAME_COUNT * SUBTITLE_RECORD_STRIDE);
    for (int line = 0; line < SUBTITLE_LINE_IDS; line++) {
        const subtitle_line *entry = &timing_tables.lines[line];
        if (!entry->part_count || entry->duration <= 0) continue;
        int voice_frames = cover_voice && subtitle_has_voice(&timing_tables, line) ? voice_length_frames(entry) : 0;
        subtitle_line_timing game, wanted;
        subtitle_game_timing(&timing_tables, line, &game);
        subtitle_apply_timing(&timing_tables, line, record_starts, record_ends, voice_frames, &wanted);
        if (!memcmp(&game, &wanted, sizeof game)) continue;
        if (wanted.end[entry->part_count - 1] > game.end[entry->part_count - 1] && voice_frames >= wanted.total) extended++;
        line_timings[line] = wanted;
        line_retimed[line] = 1;
        *(short *)(SUBTITLE_LINE_TABLE_VA + line * SUBTITLE_LINE_STRIDE + SUBTITLE_LINE_DURATION) = wanted.total;
        retimed++;
    }
    log_line("subtitles: %d lines retimed (%d timed records in files; %d kept up until their voice ends)", retimed,
             timed_records, extended);
    return retimed;
}

void install_subtitle_hooks(const char *game_folder, const char *ini_path) {
    char name[MAX_PATH] = "", language[16] = "";
    GetPrivateProfileStringA("text", "subtitles", "", name, sizeof name, ini_path);
    GetPrivateProfileStringA("text", "language", "japanese", language, sizeof language, ini_path);
    windows_font_everywhere = GetPrivateProfileIntA("text", "subtitle_windows_font", 0, ini_path);
    int cover_voice = GetPrivateProfileIntA("text", "subtitles_cover_voice", 1, ini_path);
    int size_percent = GetPrivateProfileIntA("text", "subtitle_size", 100, ini_path);
    int position_percent = GetPrivateProfileIntA("text", "subtitle_position", 0, ini_path);
    if (size_percent >= 50 && size_percent <= 300) subtitle_scale = size_percent / 100.0f;
    if (position_percent >= 10 && position_percent <= 98) subtitle_top_2d = position_percent * SCREEN_2D_HEIGHT / 100.0f;
    int moved = subtitle_top_2d != SUBTITLE_TOP_2D;
    for (int i = 0; i < SUBTITLE_COUNT; i++) record_starts[i] = record_ends[i] = SUBTITLE_NO_TIME;
    int english_mode = _stricmp(language, "english") == 0;
    char english_path[MAX_PATH];
    snprintf(english_path, sizeof english_path, "%ssubtitles\\english.tsv", game_folder);
    int english_lines = english_mode && _stricmp(name, "english") ? load_subtitles(english_path) : -1;
    if (english_lines >= 0) log_line("subtitles: %d English lines from %s (base for English mode)", english_lines, english_path);
    int loaded = english_lines > 0 ? english_lines : 0;
    char path[MAX_PATH] = "";
    if (name[0]) {
        snprintf(path, sizeof path, "%ssubtitles\\%s.tsv", game_folder, name);
        int file_lines = load_subtitles(path);
        if (file_lines < 0) log_line("subtitles: cannot read %s", path);
        else loaded += file_lines;
    }
    text_lines_loaded = loaded > 0;
    int retimed = retime_lines(cover_voice);
    MH_STATUS status = MH_ERROR_DISABLED, task_status = MH_ERROR_DISABLED;
    if (loaded > 0 || windows_font_everywhere || moved) {
        status = MH_CreateHook((void *)DRAW_SUBTITLE_VA, draw_subtitle_hook, (void **)&original_draw_subtitle);
        if (status == MH_OK) status = MH_EnableHook((void *)DRAW_SUBTITLE_VA);
    }
    if (loaded > 0 || retimed > 0) {
        task_status = MH_CreateHook((void *)SUBTITLE_TASK_VA, subtitle_task_hook, (void **)&original_subtitle_task);
        if (task_status == MH_OK) task_status = MH_EnableHook((void *)SUBTITLE_TASK_VA);
    }
    if (!loaded && !windows_font_everywhere && !retimed && !moved) return;
    if (moved || subtitle_scale != 1.0f)
        log_line("subtitles: size %d %%, line at %.0f of 480 (Windows-font lines change size; game-font lines move)",
                 (int)(subtitle_scale * 100 + 0.5f), subtitle_top_2d);
    log_line("subtitles: %d lines from %s (hook draw_subtitle: %s, subtitle task: %s); %s", loaded,
             path[0] ? path : "no file", MH_StatusToString(status), MH_StatusToString(task_status),
             _stricmp(language, "english") ? "missing lines keep the game's Japanese subtitles"
                                           : "missing lines keep the game's English subtitles");
    if (windows_font_line_count || windows_font_everywhere) install_windows_font_overlay(ini_path);
}
