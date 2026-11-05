#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "paintball.h"
#include "blood.h"
#include "voice_packs.h"
#include "log.h"
#include "MinHook.h"

#define MAX_BLOOD_SURFACES 128
#define OBJECT_SCAN_WORDS 32
#define DEVICE_SET_RENDER_STATE 20
#define DEVICE_GET_RENDER_STATE 21
#define DEVICE_GET_RENDER_TARGET 9
#define DEVICE_GET_TEXTURE_STAGE_STATE 36
#define DEVICE_SET_TEXTURE_STAGE_STATE 37
#define DEVICE_SET_TEXTURE 35
#define COM_RELEASE 2
#define RENDER_STATE_TEXTURE_FACTOR 60
#define STAGE_COLOR_OP 1
#define STAGE_COLOR_ARG1 2
#define STAGE_COLOR_ARG2 3
#define STAGE_ALPHA_OP 4
#define STAGE_ALPHA_ARG1 5
#define OP_DISABLE 1
#define OP_SELECT_ARG1 2
#define OP_MODULATE 4
#define ARG_CURRENT 1
#define ARG_TEXTURE_FACTOR 3
#define SAVED_STATES 5

typedef HRESULT(__stdcall *set_texture_fn)(void *device, DWORD stage, void *surface);
typedef HRESULT(__stdcall *set_state_fn)(void *device, DWORD state, DWORD value);
typedef HRESULT(__stdcall *get_state_fn)(void *device, DWORD state, DWORD *value);
typedef HRESULT(__stdcall *set_stage_state_fn)(void *device, DWORD stage, DWORD state, DWORD value);
typedef HRESULT(__stdcall *get_stage_state_fn)(void *device, DWORD stage, DWORD state, DWORD *value);
typedef HRESULT(__stdcall *get_render_target_fn)(void *device, void **surface);
typedef ULONG(__stdcall *release_fn)(void *self);

static void ***direct3d_device;
static void *surface_vtable;
static void *blood_surfaces[MAX_BLOOD_SURFACES];
static int blood_surface_count, tint_active, hooked;
static DWORD saved_stage_states[SAVED_STATES], saved_texture_factor;
static volatile DWORD paint_colour = 0xffff2080;
static set_texture_fn original_set_texture;
static const DWORD stage_states[SAVED_STATES] = {STAGE_COLOR_OP, STAGE_COLOR_ARG1, STAGE_COLOR_ARG2, STAGE_ALPHA_OP,
                                                 STAGE_ALPHA_ARG1};

static void *device_method(void *device, int slot) {
    return (*(void ***)device)[slot];
}

static int is_blood_surface(void *surface) {
    for (int i = 0; surface && i < blood_surface_count; i++) {
        if (blood_surfaces[i] == surface) return 1;
    }
    return 0;
}

static void start_tint(void *device) {
    if (!tint_active) {
        for (int i = 0; i < SAVED_STATES; i++)
            ((get_stage_state_fn)device_method(device, DEVICE_GET_TEXTURE_STAGE_STATE))(device, 1, stage_states[i],
                                                                                       &saved_stage_states[i]);
        ((get_state_fn)device_method(device, DEVICE_GET_RENDER_STATE))(device, RENDER_STATE_TEXTURE_FACTOR, &saved_texture_factor);
    }
    set_stage_state_fn set_stage = (set_stage_state_fn)device_method(device, DEVICE_SET_TEXTURE_STAGE_STATE);
    set_stage(device, 1, STAGE_COLOR_OP, OP_MODULATE);
    set_stage(device, 1, STAGE_COLOR_ARG1, ARG_CURRENT);
    set_stage(device, 1, STAGE_COLOR_ARG2, ARG_TEXTURE_FACTOR);
    set_stage(device, 1, STAGE_ALPHA_OP, OP_SELECT_ARG1);
    set_stage(device, 1, STAGE_ALPHA_ARG1, ARG_CURRENT);
    ((set_state_fn)device_method(device, DEVICE_SET_RENDER_STATE))(device, RENDER_STATE_TEXTURE_FACTOR, paint_colour);
    tint_active = 1;
}

static void stop_tint(void *device) {
    if (!tint_active) return;
    for (int i = 0; i < SAVED_STATES; i++)
        ((set_stage_state_fn)device_method(device, DEVICE_SET_TEXTURE_STAGE_STATE))(device, 1, stage_states[i],
                                                                                   saved_stage_states[i]);
    ((set_state_fn)device_method(device, DEVICE_SET_RENDER_STATE))(device, RENDER_STATE_TEXTURE_FACTOR, saved_texture_factor);
    tint_active = 0;
}

static HRESULT __stdcall set_texture_hook(void *device, DWORD stage, void *surface) {
    HRESULT result = original_set_texture(device, stage, surface);
    if (stage == 0) {
        if (is_blood_surface(surface)) start_tint(device);
        else stop_tint(device);
    }
    return result;
}

static DWORD vivid_colour(void) {
    static const DWORD palette[] = {0xffff2a2a, 0xffff8a00, 0xfff5e100, 0xff3ee03e, 0xff00d5ff, 0xff2f6bff,
                                    0xffa03cff, 0xffff3ec8, 0xffffffff, 0xff00ffa0};
    static int last = -1;
    int pick;
    do pick = rand() % (int)(sizeof palette / sizeof palette[0]);
    while (pick == last);
    last = pick;
    return palette[pick];
}

static void new_colour_on_blood_sound(const char *game_path) {
    if (!strstr(game_path, "BLOOD") && !strstr(game_path, "SIBUKI")) return;
    paint_colour = vivid_colour();
}

static void hook_device(void *device) {
    void *target = device_method(device, DEVICE_SET_TEXTURE);
    MH_STATUS status = MH_CreateHook(target, set_texture_hook, (void **)&original_set_texture);
    if (status == MH_OK) status = MH_EnableHook(target);
    hooked = 1;
    log_line("paintball: hook Direct3D SetTexture %s", MH_StatusToString(status));
}

static void learn_surface_vtable(void *device) {
    void *target = NULL;
    if (FAILED(((get_render_target_fn)device_method(device, DEVICE_GET_RENDER_TARGET))(device, &target)) || !target) return;
    surface_vtable = *(void **)target;
    ((release_fn)device_method(target, COM_RELEASE))(target);
}

void paintball_register_texture(void *texture_object) {
    if (!direct3d_device || !*direct3d_device || !texture_object) return;
    void *device = *direct3d_device;
    if (!surface_vtable) learn_surface_vtable(device);
    if (!hooked) hook_device(device);
    if (!surface_vtable || blood_surface_count >= MAX_BLOOD_SURFACES) return;
    void *const *words = texture_object;
    for (int i = 0; i < OBJECT_SCAN_WORDS; i++) {
        if (IsBadReadPtr(&words[i], sizeof words[i])) break;
        void *candidate = words[i];
        if (!candidate || IsBadReadPtr(candidate, sizeof(void *))) continue;
        if (*(void **)candidate != surface_vtable) continue;
        blood_surfaces[blood_surface_count++] = candidate;
        if (blood_surface_count == 1) log_line("paintball: blood texture surface found at word %d of the texture object", i);
        return;
    }
    static int missing_logged;
    if (!missing_logged++) log_line("paintball: no Direct3D surface found in a blood texture object");
}

void install_paintball(void) {
    if (!blood_is_paintball()) return;
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    direct3d_device = hglcore ? (void ***)GetProcAddress(hglcore, "g_lpD3DDevice") : NULL;
    srand(GetTickCount());
    voice_packs_observe(new_colour_on_blood_sound);
    log_line("paintball: blood takes a new colour with every blood splash sound");
}
