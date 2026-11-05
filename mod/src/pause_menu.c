#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "pause_menu.h"
#include "frame_hooks.h"
#include "frame_interp.h"
#include "online_hooks.h"
#include "log.h"
#include "achievements.h"
#include "music_player.h"
#include "sound_loops.h"
#include "MinHook.h"

#define GAME_MODE (*(volatile int *)0x00a3e308)
#define GAME_TYPE (*(volatile int *)0x00a3e248)
#define REQUESTED_MODE (*(volatile int *)0x00757c58)
#define GAMEPLAY_MODE 6
#define STAGE (*(volatile short *)0x00a19598)
#define MODE_STEP (*(volatile int *)0x009f3a84)
#define LOAD_STAGE_EVENTS_VA 0x004804b0
#define SKIPPABLE_CUTSCENE (*(volatile int *)0x00a18fa8)
#define ENTER_SCANCODE 0x1c
#define PLAYER_STATE(p) (*(volatile short *)(0x00a18622 + (p) * 0x210))
#define PLAYER_NOT_PLAYING 9
#define PLAYER_JOINING 0
#define CONFIRM_FIRST_FRAME 45
#define CONFIRM_EVERY_FRAMES 30
#define CONFIRM_PRESS_FRAMES 3
#define CONFIRM_LAST_FRAME 1800
#define NO_MODE_REQUEST (-1)
#define DEVICE_GET_RENDER_TARGET 9
#define SURFACE_RELEASE 2
#define SURFACE_GET_DC 17
#define SURFACE_GET_DESC 22
#define SURFACE_RELEASE_DC 26
#define SURFACE_DESC_BYTES 124
#define SCREEN_2D_HEIGHT 480.0f
#define CAPTURE_TIMEOUT_FRAMES 4

typedef HRESULT(__stdcall *com_call_fn)(void *self, void *argument);
typedef ULONG(__stdcall *com_release_fn)(void *self);

enum pause_state { RUNNING, CAPTURING, PAUSED, LEAVING, PHOTO };
enum menu_item { ITEM_RESUME, ITEM_RESTART, ITEM_EXIT, ITEM_COUNT };

static const wchar_t *const item_names[ITEM_COUNT] = {L"Resume", L"Restart stage", L"Exit to menu"};
static int state = RUNNING, selected, capture_frames, pending_action = -1;
static int menu_enabled, photo_enabled, photo_requested, save_requested, saved_message_frames;
static wchar_t saved_name[64];
static char photos_folder[MAX_PATH];
static int log_frozen_frame;
static int exit_mode = 4, restart_mode = GAMEPLAY_MODE;
static void ***direct3d_device;
static HFONT title_font, item_font;
static int font_height;
static int last_logged_mode = -2;
static int restart_stage = -1, forced_restart_stage = -1;
typedef void(__cdecl *load_stage_events_fn)(void);
static load_stage_events_fn original_load_stage_events;
static int game_window_in_front(void) {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

static int confirming_mode_select;
static int confirm_frames;

static void send_enter(DWORD flags) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_RETURN;
    input.ki.wScan = ENTER_SCANCODE;
    input.ki.dwFlags = flags;
    SendInput(1, &input, sizeof input);
}

static void press_enter_for_mode_select(void) {
    if (!confirming_mode_select) return;
    if (SKIPPABLE_CUTSCENE || GAME_MODE != GAMEPLAY_MODE || ++confirm_frames > CONFIRM_LAST_FRAME) {
        log_line("pause: stopped confirming Mode Select after %d frames (%s)", confirm_frames,
                 SKIPPABLE_CUTSCENE ? "opening started" : "timeout");
        confirming_mode_select = 0;
        return;
    }
    if (confirm_frames < CONFIRM_FIRST_FRAME || !game_window_in_front()) return;
    int phase = (confirm_frames - CONFIRM_FIRST_FRAME) % CONFIRM_EVERY_FRAMES;
    if (phase == 0) {
        send_enter(0);
        log_line("pause: Enter for Mode Select (frame %d)", confirm_frames);
    }
    if (phase == CONFIRM_PRESS_FRAMES) send_enter(KEYEVENTF_KEYUP);
}

static void __cdecl load_stage_events_hook(void) {
    if (restart_stage >= 0 && GAME_MODE == GAMEPLAY_MODE && MODE_STEP == 0) {
        log_line("pause: restarting at stage %d (the game picked %d)", restart_stage, STAGE);
        STAGE = (short)restart_stage;
        if (restart_stage == 0) {
            confirming_mode_select = 1;
            confirm_frames = 0;
        }
        restart_stage = -1;
    }
    original_load_stage_events();
}

static void *com_method(void *object, int slot) {
    return (*(void ***)object)[slot];
}

static int key_pressed(int vk) {
    return (GetAsyncKeyState(vk) & 1) != 0;
}

static int key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void forget_key_presses(void) {
    static const int keys[] = {VK_ESCAPE, VK_RETURN, VK_UP, VK_DOWN, VK_SPACE, VK_F12};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) GetAsyncKeyState(keys[i]);
}


static int pause_allowed(void) {
    return GAME_MODE == GAMEPLAY_MODE && REQUESTED_MODE == NO_MODE_REQUEST && !online_lockstep_active();
}

static void make_fonts(int height) {
    int em = (int)(30.0f * (float)height / SCREEN_2D_HEIGHT + 0.5f);
    if (title_font && font_height == em) return;
    if (title_font) DeleteObject(title_font);
    if (item_font) DeleteObject(item_font);
    title_font = CreateFontW(-em * 4 / 3, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
    item_font = CreateFontW(-em, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
    font_height = em;
}

static void draw_centered(HDC dc, int width, int y, const wchar_t *text, COLORREF color) {
    SIZE size;
    int length = lstrlenW(text);
    GetTextExtentPoint32W(dc, text, length, &size);
    int x = (width - size.cx) / 2;
    SetTextColor(dc, RGB(0, 0, 0));
    for (int dy = -2; dy <= 2; dy += 2) {
        for (int dx = -2; dx <= 2; dx += 2) {
            if (dx || dy) TextOutW(dc, x + dx, y + dy, text, length);
        }
    }
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text, length);
}

static void draw_menu(HDC dc, int width, int height) {
    make_fonts(height);
    int line = font_height * 3 / 2;
    int panel_width = width * 2 / 5, panel_height = line * (ITEM_COUNT + 3);
    if (panel_width < 300) panel_width = 300;
    RECT panel = {(width - panel_width) / 2, (height - panel_height) / 2, (width + panel_width) / 2, (height + panel_height) / 2};
    HBRUSH background = CreateSolidBrush(RGB(16, 8, 8));
    FillRect(dc, &panel, background);
    DeleteObject(background);
    HBRUSH border = CreateSolidBrush(RGB(160, 20, 20));
    FrameRect(dc, &panel, border);
    RECT inner = {panel.left + 2, panel.top + 2, panel.right - 2, panel.bottom - 2};
    FrameRect(dc, &inner, border);
    DeleteObject(border);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, title_font);
    int top = panel.top + line / 2;
    draw_centered(dc, width, top, L"PAUSE", RGB(220, 30, 30));
    SelectObject(dc, item_font);
    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = top + line * 2 + i * line;
        if (i == selected) {
            wchar_t marked[64];
            wsprintfW(marked, L"> %s <", item_names[i]);
            draw_centered(dc, width, y, marked, RGB(255, 230, 80));
        } else {
            draw_centered(dc, width, y, item_names[i], RGB(230, 230, 230));
        }
    }
    SelectObject(dc, previous);
}

typedef struct {
    UINT32 version;
    void *debug_event_callback;
    BOOL suppress_background_thread;
    BOOL suppress_external_codecs;
} photo_gdiplus_input;
int WINAPI GdiplusStartup(ULONG_PTR *token, const photo_gdiplus_input *input, void *output);
int WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP bitmap, HPALETTE palette, void **image);
int WINAPI GdipSaveImageToFile(void *image, const WCHAR *file, const CLSID *encoder, const void *parameters);
int WINAPI GdipDisposeImage(void *image);
static const CLSID PNG_ENCODER = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};

static int save_photo(HDC dc, int width, int height) {
    static ULONG_PTR token;
    if (!token) {
        photo_gdiplus_input input = {1, NULL, FALSE, FALSE};
        if (GdiplusStartup(&token, &input, NULL) != 0) return 0;
    }
    HDC copy = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ previous = SelectObject(copy, bitmap);
    BitBlt(copy, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(copy, previous);
    DeleteDC(copy);
    CreateDirectoryA(photos_folder, NULL);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t path[MAX_PATH];
    wsprintfW(saved_name, L"%04d-%02d-%02d_%02d%02d%02d.png", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond);
    wsprintfW(path, L"%hs%s", photos_folder, saved_name);
    void *image = NULL;
    int ok = GdipCreateBitmapFromHBITMAP(bitmap, NULL, &image) == 0 && GdipSaveImageToFile(image, path, &PNG_ENCODER, NULL) == 0;
    if (image) GdipDisposeImage(image);
    DeleteObject(bitmap);
    log_line("photo: %s photos\\%ls", ok ? "saved" : "could not save", saved_name);
    return ok;
}

static void draw_photo_hint(HDC dc, int width, int height) {
    make_fonts(height);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, item_font);
    wchar_t text[128];
    if (saved_message_frames > 0) wsprintfW(text, L"Saved photos\\%s", saved_name);
    else lstrcpyW(text, L"PHOTO MODE   F12 save   Esc back");
    draw_centered(dc, width, height - font_height * 2, text, RGB(255, 230, 80));
    SelectObject(dc, previous);
}

static void draw_photo_mode(void) {
    if (!direct3d_device || !*direct3d_device) return;
    void *surface = NULL;
    if (FAILED(((com_call_fn)com_method(*direct3d_device, DEVICE_GET_RENDER_TARGET))(*direct3d_device, &surface)) || !surface) return;
    DWORD desc[SURFACE_DESC_BYTES / 4] = {SURFACE_DESC_BYTES};
    HDC dc = NULL;
    if (SUCCEEDED(((com_call_fn)com_method(surface, SURFACE_GET_DESC))(surface, desc)) &&
        SUCCEEDED(((com_call_fn)com_method(surface, SURFACE_GET_DC))(surface, &dc))) {
        if (save_requested) {
            save_requested = 0;
            if (save_photo(dc, (int)desc[3], (int)desc[2])) {
                saved_message_frames = 120;
                achievements_unlock("photographer");
            }
        }
        draw_photo_hint(dc, (int)desc[3], (int)desc[2]);
        ((com_call_fn)com_method(surface, SURFACE_RELEASE_DC))(surface, dc);
    }
    ((com_release_fn)com_method(surface, SURFACE_RELEASE))(surface);
}

static void draw_menu_on_render_target(void) {
    if (!direct3d_device || !*direct3d_device) return;
    void *surface = NULL;
    if (FAILED(((com_call_fn)com_method(*direct3d_device, DEVICE_GET_RENDER_TARGET))(*direct3d_device, &surface)) || !surface) return;
    DWORD desc[SURFACE_DESC_BYTES / 4] = {SURFACE_DESC_BYTES};
    HDC dc = NULL;
    if (SUCCEEDED(((com_call_fn)com_method(surface, SURFACE_GET_DESC))(surface, desc)) &&
        SUCCEEDED(((com_call_fn)com_method(surface, SURFACE_GET_DC))(surface, &dc))) {
        draw_menu(dc, (int)desc[3], (int)desc[2]);
        ((com_call_fn)com_method(surface, SURFACE_RELEASE_DC))(surface, dc);
    }
    ((com_release_fn)com_method(surface, SURFACE_RELEASE))(surface);
}

typedef int(__cdecl *sound_control_fn)(int unused, unsigned command);
#define PAUSE_MUSIC 0x2a0
#define RESUME_MUSIC 0x3a0

static void pause_sound(int on) {
    static sound_control_fn sound_control;
    if (!sound_control) {
        HMODULE hglna = GetModuleHandleA("HglNa.dll");
        sound_control = hglna ? (sound_control_fn)GetProcAddress(hglna, "nlsndSystemControl") : NULL;
    }
    if (sound_control) sound_control(0, on ? PAUSE_MUSIC : RESUME_MUSIC);
    music_player_pause(on);
    sound_loops_pause(on);
}

static void leave_pause(void) {
    pause_sound(0);
    frame_interp_set_frozen(0);
    state = RUNNING;
    forget_key_presses();
    log_line("pause: %s", pending_action == ITEM_RESTART ? "restart stage" : pending_action == ITEM_EXIT ? "exit to menu" : "resumed");
    if (pending_action == ITEM_EXIT) REQUESTED_MODE = exit_mode;
    if (pending_action == ITEM_RESTART) {
        restart_stage = forced_restart_stage >= 0 ? forced_restart_stage : STAGE;
        for (int p = 0; p < 2; p++) {
            if (PLAYER_STATE(p) != PLAYER_NOT_PLAYING) PLAYER_STATE(p) = PLAYER_JOINING;
        }
        REQUESTED_MODE = restart_mode;
    }
    pending_action = -1;
}

static void handle_menu_keys(void) {
    if (!game_window_in_front()) {
        forget_key_presses();
        return;
    }
    if (key_pressed(VK_UP)) selected = (selected + ITEM_COUNT - 1) % ITEM_COUNT;
    if (key_pressed(VK_DOWN)) selected = (selected + 1) % ITEM_COUNT;
    if (key_pressed(VK_ESCAPE)) {
        pending_action = ITEM_RESUME;
        state = LEAVING;
    } else if (key_pressed(VK_RETURN) || key_pressed(VK_SPACE)) {
        pending_action = selected;
        state = LEAVING;
    }
}

static void log_mode_changes(void) {
    if (GAME_MODE == last_logged_mode) return;
    log_line("pause: game mode %d (game type %d)", GAME_MODE, GAME_TYPE);
    last_logged_mode = GAME_MODE;
}

static int __cdecl pause_frame(void) {
    log_mode_changes();
    switch (state) {
    case RUNNING: {
        int escape = key_pressed(VK_ESCAPE) && menu_enabled, photo = key_pressed(VK_F12) && photo_enabled;
        if ((escape || photo) && game_window_in_front() && pause_allowed()) {
            frame_interp_capture_next_frame();
            capture_frames = 0;
            photo_requested = photo && !escape;
            state = CAPTURING;
        }
        return 0;
    }
    case PHOTO:
        if (saved_message_frames > 0) saved_message_frames--;
        if (game_window_in_front() && key_pressed(VK_F12)) save_requested = 1;
        if (game_window_in_front() && key_pressed(VK_ESCAPE)) {
            pending_action = ITEM_RESUME;
            state = LEAVING;
        }
        frame_interp_draw_frozen_scene();
        if (state == PHOTO) draw_photo_mode();
        return 1;
    case CAPTURING:
        if (!frame_interp_frame_ready() && ++capture_frames < CAPTURE_TIMEOUT_FRAMES) return 0;
        frame_interp_set_frozen(1);
        selected = ITEM_RESUME;
        forget_key_presses();
        state = photo_requested ? PHOTO : PAUSED;
        static int frozen_logged;
        if (photo_requested && log_frozen_frame && !frozen_logged++) frame_interp_log_frozen_matrices();
        pause_sound(1);
        log_line("pause: paused at stage %d, step %d (frozen frame %s)", STAGE, MODE_STEP,
                 frame_interp_frame_ready() ? "kept" : "missing");
        break;
    case PAUSED:
        handle_menu_keys();
        break;
    case LEAVING:
        if (key_down(VK_RETURN) || key_down(VK_ESCAPE) || key_down(VK_SPACE) || key_down(VK_F12)) break;
        leave_pause();
        return 0;
    }
    if (photo_requested) {
        frame_interp_draw_frozen_scene();
    } else {
        frame_interp_draw_frozen();
        draw_menu_on_render_target();
    }
    return 1;
}

void install_pause_menu(const char *ini_path) {
    menu_enabled = GetPrivateProfileIntA("gameplay", "pause_menu", 1, ini_path);
    photo_enabled = GetPrivateProfileIntA("fun", "photo_mode", 0, ini_path);
    log_frozen_frame = GetPrivateProfileIntA("debug", "log_frozen_frame", 0, ini_path);
    if (!menu_enabled && !photo_enabled) return;
    lstrcpynA(photos_folder, ini_path, MAX_PATH);
    char *slash = strrchr(photos_folder, '\\');
    if (slash) lstrcpyA(slash + 1, "photos\\");
    exit_mode = GetPrivateProfileIntA("debug", "pause_exit_mode", exit_mode, ini_path);
    restart_mode = GetPrivateProfileIntA("debug", "pause_restart_mode", restart_mode, ini_path);
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    direct3d_device = hglcore ? (void ***)GetProcAddress(hglcore, "g_lpD3DDevice") : NULL;
    forced_restart_stage = GetPrivateProfileIntA("debug", "pause_restart_stage", -1, ini_path);
    MH_STATUS status = MH_CreateHook((void *)LOAD_STAGE_EVENTS_VA, load_stage_events_hook, (void **)&original_load_stage_events);
    if (status == MH_OK) status = MH_EnableHook((void *)LOAD_STAGE_EVENTS_VA);
    log_line("pause: hook load_stage_events %s", MH_StatusToString(status));
    frame_hooks_on_frame_end(press_enter_for_mode_select);
    frame_hooks_set_pause_handler(pause_frame);
    log_line("pause: %s%s%s", menu_enabled ? "Esc opens the pause menu during play" : "",
             menu_enabled && photo_enabled ? "; " : "", photo_enabled ? "F12 opens photo mode (photos\\)" : "");
}
