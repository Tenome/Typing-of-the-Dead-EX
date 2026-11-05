#include <windows.h>
#include <math.h>
#include <stdint.h>
#include "display_hooks.h"
#include "log.h"
#include "MinHook.h"

#define HGLCORE_GHIDRA_BASE 0x10000000
#define SET_RESOLUTION_VA 0x10002180
#define CREATE_WINDOW_VA 0x100041d0
#define SCREEN_WIDTH_VA 0x10019420
#define SCREEN_HEIGHT_VA 0x10019424
#define SCALE_2D_X_VA 0x10019374
#define SCALE_2D_Y_VA 0x10019370
#define CURRENT_TEXTURE_VA 0x10019350
#define MAIN_WINDOW_VA 0x10019418
#define FULLSCREEN_FLAG_VA 0x10019428
#define MAX_SIZE_FRAME_ALLOWANCE 64
#define SETUP_3D_VIEW_VA 0x0048b370
#define HUD_PROJECTION_SCALE (*(float *)0x00a3e994)
#define ANGLE_UNITS_PER_TURN 65536.0
#define GAME_FOV_DEGREES 41
#define GAME_HALF_FOV_RADIANS 0.35866388296751145

#define AUTHORED_WIDTH 640.0f
#define AUTHORED_HEIGHT 480.0f
#define AUTHORED_ASPECT (4.0f / 3.0f)
#define TLV_FLOATS_PER_VERTEX 8
#define TLV_DIFFUSE 4
#define TLV_TU 6
#define TLV_TV 7
#define FULL_WIDTH_EDGE_TOLERANCE 2.0f
#define EDGE_DRAW_MIN_WIDTH 300.0f
#define MAX_LOGGED_WIDE_DRAWS 256
#define SURFACE_GET_DESC_SLOT 22
#define SURFACE_DESC2_BYTES 124

typedef int(__cdecl *set_resolution_fn)(int a, int b, int fullscreen);
typedef int(__cdecl *draw_strip_tlv_fn)(float *vertices, unsigned count);
typedef int(__cdecl *create_window_fn)(HINSTANCE instance);
typedef void(__cdecl *perspective_fn)(int angle, float aspect, float near_plane, float far_plane);
typedef int(__cdecl *profile_flag_fn)(const char *section, const char *key);
typedef int(__cdecl *init_system_fn)(int mode);

static set_resolution_fn original_set_resolution;
static draw_strip_tlv_fn original_draw_strip_tlv;
static perspective_fn original_perspective;
static create_window_fn original_create_window;
static profile_flag_fn original_profile_flag;
static init_system_fn original_init_system;
static int borderless;
static int fov_degrees;
typedef void(__cdecl *setup_3d_view_fn)(void);
static setup_3d_view_fn original_setup_3d_view;

static unsigned char *hglcore_base;
static int configured_width, configured_height, widescreen, stretch_full_width_2d, log_wide_draws;
static unsigned logged_wide_draw_signatures[MAX_LOGGED_WIDE_DRAWS];
static int logged_wide_draw_count;
static float offset_2d_x_authored;

static void *hglcore_address(DWORD ghidra_va) {
    return hglcore_base + (ghidra_va - HGLCORE_GHIDRA_BASE);
}

static int screen_width(void) { return *(int *)hglcore_address(SCREEN_WIDTH_VA); }
static int screen_height(void) { return *(int *)hglcore_address(SCREEN_HEIGHT_VA); }

static void apply_2d_layout(void) {
    float width = (float)screen_width(), height = (float)screen_height();
    float scale_x = width / AUTHORED_WIDTH, scale_y = height / AUTHORED_HEIGHT;
    offset_2d_x_authored = 0.0f;
    if (widescreen && width / height > AUTHORED_ASPECT + 0.001f) {
        scale_x = scale_y;
        offset_2d_x_authored = (width - AUTHORED_WIDTH * scale_y) / 2.0f / scale_y;
    }
    *(float *)hglcore_address(SCALE_2D_X_VA) = scale_x;
    *(float *)hglcore_address(SCALE_2D_Y_VA) = scale_y;
    log_line("display: %dx%d, 2D scale %.4f x %.4f, 2D offset %.1f px", screen_width(), screen_height(), scale_x,
             scale_y, offset_2d_x_authored * scale_y);
}

static int __cdecl set_resolution_hook(int a, int b, int fullscreen) {
    int result = original_set_resolution(a, b, fullscreen);
    if (configured_width > 0 && configured_height > 0) {
        *(int *)hglcore_address(SCREEN_WIDTH_VA) = configured_width;
        *(int *)hglcore_address(SCREEN_HEIGHT_VA) = configured_height;
    }
    apply_2d_layout();
    return result;
}

typedef struct {
    float min_x, max_x, min_y, max_y, min_u, max_u, min_v, max_v;
} tlv_bounds;

static tlv_bounds measure_tlv(const float *vertices, unsigned count) {
    tlv_bounds b = {1e9f, -1e9f, 1e9f, -1e9f, 1e9f, -1e9f, 1e9f, -1e9f};
    for (unsigned i = 0; i < count; i++) {
        const float *v = vertices + i * TLV_FLOATS_PER_VERTEX;
        b.min_x = fminf(b.min_x, v[0]), b.max_x = fmaxf(b.max_x, v[0]);
        b.min_y = fminf(b.min_y, v[1]), b.max_y = fmaxf(b.max_y, v[1]);
        b.min_u = fminf(b.min_u, v[TLV_TU]), b.max_u = fmaxf(b.max_u, v[TLV_TU]);
        b.min_v = fminf(b.min_v, v[TLV_TV]), b.max_v = fmaxf(b.max_v, v[TLV_TV]);
    }
    return b;
}

static void texture_size(void *texture, unsigned *width, unsigned *height) {
    *width = *height = 0;
    if (!texture) return;
    DWORD desc[SURFACE_DESC2_BYTES / 4] = {SURFACE_DESC2_BYTES};
    typedef HRESULT(__stdcall * get_desc_fn)(void *surface, void *desc);
    get_desc_fn get_desc = (*(get_desc_fn **)texture)[SURFACE_GET_DESC_SLOT];
    if (SUCCEEDED(get_desc(texture, desc))) {
        *height = desc[2];
        *width = desc[3];
    }
}

static int spans_authored_width(const tlv_bounds *b) {
    return fabsf(b->min_x) <= FULL_WIDTH_EDGE_TOLERANCE && fabsf(b->max_x - AUTHORED_WIDTH) <= FULL_WIDTH_EDGE_TOLERANCE;
}

static int touches_top_or_bottom(const tlv_bounds *b) {
    return b->max_x - b->min_x >= EDGE_DRAW_MIN_WIDTH &&
           (b->min_y <= FULL_WIDTH_EDGE_TOLERANCE || b->max_y >= AUTHORED_HEIGHT - FULL_WIDTH_EDGE_TOLERANCE);
}

static void log_2d_draw(const char *kind, const float *vertices, unsigned count, const tlv_bounds *b) {
    if (logged_wide_draw_count == MAX_LOGGED_WIDE_DRAWS) return;
    void *texture = *(void **)hglcore_address(CURRENT_TEXTURE_VA);
    unsigned signature = (unsigned)(uintptr_t)texture * 31u + (unsigned)b->min_y * 7919u + (unsigned)b->max_y * 104729u +
                         (unsigned)b->min_x * 13u + count;
    for (int i = 0; i < logged_wide_draw_count; i++) {
        if (logged_wide_draw_signatures[i] == signature) return;
    }
    logged_wide_draw_signatures[logged_wide_draw_count++] = signature;
    unsigned texture_width, texture_height;
    texture_size(texture, &texture_width, &texture_height);
    log_line("%s 2D: x %.1f..%.1f y %.1f..%.1f, %u vertices, colour %08X, uv %.3f..%.3f x %.3f..%.3f, texture %p %ux%u",
             kind, b->min_x, b->max_x, b->min_y, b->max_y, count, *(const unsigned *)&vertices[TLV_DIFFUSE], b->min_u, b->max_u, b->min_v,
             b->max_v, texture, texture_width, texture_height);
}

static void stretch_to_screen_width(float *vertices, unsigned count) {
    float stretch = 1.0f + 2.0f * offset_2d_x_authored / AUTHORED_WIDTH;
    for (unsigned i = 0; i < count; i++) {
        float *x = &vertices[i * TLV_FLOATS_PER_VERTEX];
        *x = *x * stretch - offset_2d_x_authored;
    }
}

static int __cdecl draw_strip_tlv_hook(float *vertices, unsigned count) {
    if (offset_2d_x_authored != 0.0f && (stretch_full_width_2d || log_wide_draws)) {
        tlv_bounds bounds = measure_tlv(vertices, count);
        int full_width = spans_authored_width(&bounds);
        if (log_wide_draws && full_width) log_2d_draw("full-width", vertices, count, &bounds);
        if (log_wide_draws && !full_width && touches_top_or_bottom(&bounds)) log_2d_draw("edge", vertices, count, &bounds);
        if (full_width && stretch_full_width_2d) stretch_to_screen_width(vertices, count);
    }
    if (offset_2d_x_authored != 0.0f) {
        for (unsigned i = 0; i < count; i++) vertices[i * TLV_FLOATS_PER_VERTEX] += offset_2d_x_authored;
    }
    return original_draw_strip_tlv(vertices, count);
}

static WNDPROC original_window_procedure;

static LRESULT CALLBACK window_procedure_hook(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    LRESULT result = CallWindowProcA(original_window_procedure, window, message, wparam, lparam);
    if (message == WM_GETMINMAXINFO) {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMaxSize.x += MAX_SIZE_FRAME_ALLOWANCE;
        limits->ptMaxSize.y += MAX_SIZE_FRAME_ALLOWANCE;
        limits->ptMaxTrackSize.x += MAX_SIZE_FRAME_ALLOWANCE;
        limits->ptMaxTrackSize.y += MAX_SIZE_FRAME_ALLOWANCE;
    }
    return result;
}

static void size_window_client_area(void) {
    HWND window = *(HWND *)hglcore_address(MAIN_WINDOW_VA);
    if (!window || borderless || *(unsigned char *)hglcore_address(FULLSCREEN_FLAG_VA)) return;
    if (!original_window_procedure) {
        original_window_procedure = (WNDPROC)SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)window_procedure_hook);
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        RECT outer, client;
        GetWindowRect(window, &outer);
        GetClientRect(window, &client);
        int missing_width = screen_width() - client.right, missing_height = screen_height() - client.bottom;
        if (!missing_width && !missing_height) break;
        SetWindowPos(window, NULL, 0, 0, outer.right - outer.left + missing_width, outer.bottom - outer.top + missing_height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT client;
    GetClientRect(window, &client);
    log_line("display: window client area %ldx%ld", client.right, client.bottom);
}

static int __cdecl profile_flag_hook(const char *section, const char *key) {
    if (section && key && !lstrcmpiA(section, "HGLCORE") && !lstrcmpiA(key, "Window")) return 1;
    return original_profile_flag(section, key);
}

static void make_window_borderless(void) {
    HWND window = *(HWND *)hglcore_address(MAIN_WINDOW_VA);
    if (!window) return;
    MONITORINFO monitor = {sizeof monitor};
    if (!GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor)) return;
    RECT area = monitor.rcMonitor;
    LONG style = GetWindowLongA(window, GWL_STYLE);
    SetWindowLongA(window, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP);
    SetWindowLongA(window, GWL_EXSTYLE, GetWindowLongA(window, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
    SetWindowPos(window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    RECT client;
    GetClientRect(window, &client);
    log_line("display: borderless window %ldx%ld at %ld,%ld (game renders %dx%d)", client.right, client.bottom, area.left,
             area.top, screen_width(), screen_height());
}

static int __cdecl init_system_hook(int mode) {
    int result = original_init_system(mode);
    if (result) make_window_borderless();
    return result;
}

static int __cdecl create_window_hook(HINSTANCE instance) {
    int result = original_create_window(instance);
    if (result && borderless) make_window_borderless();
    else if (result) size_window_client_area();
    return result;
}

static double fov_half_radians(void) {
    return fov_degrees * 3.14159265358979 / 360.0;
}

static void __cdecl perspective_hook(int angle, float aspect, float near_plane, float far_plane) {
    if (widescreen && fabsf(aspect - AUTHORED_ASPECT) < 0.001f) {
        aspect = (float)screen_width() / (float)screen_height();
    }
    if (fov_degrees) angle = (int)(fov_degrees * ANGLE_UNITS_PER_TURN / 360.0 + 0.5);
    original_perspective(angle, aspect, near_plane, far_plane);
}

static void __cdecl setup_3d_view_hook(void) {
    original_setup_3d_view();
    if (fov_degrees) HUD_PROJECTION_SCALE = (float)(240.0 / tan(fov_half_radians()));
}

static void install_hook(void *target, void *hook, void **original, const char *name) {
    MH_STATUS status = target ? MH_CreateHook(target, hook, original) : MH_ERROR_FUNCTION_NOT_FOUND;
    if (status == MH_OK) status = MH_EnableHook(target);
    log_line("hook %s at %p: %s", name, target, MH_StatusToString(status));
}

float display_fov_factor(void) {
    if (!fov_degrees) return 1.0f;
    return (float)(tan(fov_half_radians()) / tan(GAME_HALF_FOV_RADIANS));
}

float display_widescreen_factor(void) {
    if (!widescreen || !hglcore_base || screen_height() <= 0) return 1.0f;
    float factor = (float)screen_width() / (float)screen_height() / AUTHORED_ASPECT;
    return factor > 1.0f ? factor : 1.0f;
}

void install_display_hooks(const char *ini_path) {
    configured_width = GetPrivateProfileIntA("display", "width", 0, ini_path);
    configured_height = GetPrivateProfileIntA("display", "height", 0, ini_path);
    widescreen = GetPrivateProfileIntA("display", "widescreen", 1, ini_path);
    borderless = GetPrivateProfileIntA("display", "borderless", 0, ini_path);
    if (borderless) {
        SetProcessDPIAware();
        if (configured_width <= 0 || configured_height <= 0) {
            configured_width = GetSystemMetrics(SM_CXSCREEN);
            configured_height = GetSystemMetrics(SM_CYSCREEN);
        }
    }
    stretch_full_width_2d = GetPrivateProfileIntA("display", "stretch_full_width_2d", 1, ini_path);
    log_wide_draws = GetPrivateProfileIntA("display", "log_wide_2d", 0, ini_path);
    log_line("display: width %d, height %d (0 = Configurater setting), widescreen %d", configured_width,
             configured_height, widescreen);
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    if (!hglcore || !hglna) {
        log_line("display: HglCore.dll or HglNa.dll not loaded, display hooks disabled");
        return;
    }
    hglcore_base = (unsigned char *)hglcore;
    install_hook(hglcore_address(SET_RESOLUTION_VA), set_resolution_hook, (void **)&original_set_resolution,
                 "set_resolution");
    install_hook(GetProcAddress(hglcore, "D3DDrawPrimitiveStripTLV"), draw_strip_tlv_hook,
                 (void **)&original_draw_strip_tlv, "D3DDrawPrimitiveStripTLV");
    install_hook(hglcore_address(CREATE_WINDOW_VA), create_window_hook, (void **)&original_create_window,
                 "create_window");
    install_hook(GetProcAddress(hglna, "nlPerspective"), perspective_hook, (void **)&original_perspective,
                 "nlPerspective");
    fov_degrees = GetPrivateProfileIntA("display", "fov", 0, ini_path);
    if (fov_degrees < 20 || fov_degrees > 110 || fov_degrees == GAME_FOV_DEGREES) fov_degrees = 0;
    if (fov_degrees) {
        install_hook((void *)SETUP_3D_VIEW_VA, setup_3d_view_hook, (void **)&original_setup_3d_view, "setup_3d_view");
        log_line("display: vertical field of view %d degrees (game %d)", fov_degrees, GAME_FOV_DEGREES);
    }
    if (borderless) {
        install_hook(GetProcAddress(hglcore, "hlbIsProfile"), profile_flag_hook, (void **)&original_profile_flag,
                     "hlbIsProfile (borderless: always windowed)");
        install_hook(GetProcAddress(hglcore, "hlInitSystem"), init_system_hook, (void **)&original_init_system,
                     "hlInitSystem (borderless window)");
    }
}
