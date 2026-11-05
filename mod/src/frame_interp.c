#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "frame_interp.h"
#include "timing_hooks.h"
#include "subtitle_hooks.h"
#include "log.h"
#include "MinHook.h"

#define MAX_CALLS 8192
#define DATA_POOL_BYTES (2 * 1024 * 1024)
#define SCRATCH_BYTES (256 * 1024)
#define OCCURRENCE_SLOTS 16384
#define MATRIX_FLOATS 16
#define MATRIX_TRANSLATION 12
#define SPRITE_BYTES 0x38
#define LIGHT_VECTOR_BYTES 12
#define TLV_FLOATS_PER_VERTEX 8
#define TLV_BYTES_PER_VERTEX (TLV_FLOATS_PER_VERTEX * 4)
#define MATRIX_JUMP_TRANSLATION 60.0f
#define MATRIX_JUMP_ROTATION 0.6f
#define SPRITE_JUMP_PIXELS 160.0f
#define D3DX_UPDATE_NOVSYNC 1
#define D3DX_UPDATE_FRAME_SLOT 19
#define AUTHORED_UPDATE_RATE 60
#define REPORT_UPDATES 600
#define HGLCORE_GHIDRA_BASE 0x10000000
#define VSYNC_PRESENT_FLAGS_VA 0x10002ef1

typedef uint32_t(__cdecl *call4_fn)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef void(__cdecl *matrix_fn)(float *matrix);
typedef int(__cdecl *set_rate_fn)(unsigned rate);

enum call_kind { CALL_STATE, CALL_HELD };
enum payload_kind { PAYLOAD_NONE, PAYLOAD_SPRITE, PAYLOAD_LIGHT_VECTOR, PAYLOAD_TLV_STRIP };
enum blend_kind { BLEND_NONE, BLEND_MATRIX, BLEND_SPRITE, BLEND_STRIP };

typedef struct {
    const char *module;
    const char *name;
    unsigned char kind;
    unsigned char payload;
    unsigned char blend;
    unsigned char ends_frame;
    call4_fn original;
} recorded_function;

static recorded_function functions[] = {
    {"HglNa.dll", "nlObjPut", CALL_HELD, PAYLOAD_NONE, BLEND_MATRIX},
    {"HglNa.dll", "nlObjPutTrnsl", CALL_HELD, PAYLOAD_NONE, BLEND_MATRIX},
    {"HglNa.dll", "nlObjPutML", CALL_HELD, PAYLOAD_NONE, BLEND_MATRIX},
    {"HglNa.dll", "nlObjPutMLTrnsl", CALL_HELD, PAYLOAD_NONE, BLEND_MATRIX},
    {"HglNa.dll", "nlObjPutCheapEnvMap", CALL_HELD, PAYLOAD_NONE, BLEND_MATRIX},
    {"HglNa.dll", "nlScrPutCore", CALL_HELD, PAYLOAD_SPRITE, BLEND_SPRITE},
    {"HglNa.dll", "nlBeginScene", CALL_HELD},
    {"HglNa.dll", "nlEndScene", CALL_HELD},
    {"HglCore.dll", "ClearScreen", CALL_HELD},
    {"HglCore.dll", "D3DSetTexture", CALL_HELD},
    {"HglCore.dll", "D3DDrawPrimitiveStripTLV", CALL_HELD, PAYLOAD_TLV_STRIP, BLEND_STRIP},
    {"HglNa.dll", "nlObjPutSetLightDirectionV", CALL_STATE, PAYLOAD_LIGHT_VECTOR},
    {"HglNa.dll", "nlObjPutSetLightDirectionF", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetLayer", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetFadeColor", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetIngnoreZ", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetGlobalAmbience", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetSortOffset", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetLightOff", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetSrcBlend", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetDstBlend", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetZFunc", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetZWrite", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetCullMode", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetFogEnable", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetDiffuseFactor", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetAmbientFactor", CALL_STATE},
    {"HglNa.dll", "nlObjPutSetFadeDirectionalFactor", CALL_STATE},
    {"HglNa.dll", "nlScrPutSetLayer", CALL_STATE},
    {"HglNa.dll", "nlSetTransparency", CALL_STATE},
    {"HglNa.dll", "nlFogRange", CALL_STATE},
    {"HglNa.dll", "nlFogTableColor", CALL_STATE},
    {"HglNa.dll", "nlFogDensity", CALL_STATE},
    {"HglNa.dll", "nlLightViewTrans", CALL_STATE},
    {"HglNa.dll", "nlLightMode", CALL_STATE},
    {"HglNa.dll", "nlLightAllOff", CALL_STATE},
    {"HglNa.dll", "nlWaitVSync", CALL_STATE, PAYLOAD_NONE, BLEND_NONE, 1},
};
#define FUNCTION_COUNT ((int)(sizeof functions / sizeof functions[0]))

typedef struct {
    uint16_t function;
    uint16_t occurrence;
    uint32_t args[4];
    float matrix[MATRIX_FLOATS];
    uint32_t data_offset;
    uint32_t data_size;
} recorded_call;

typedef struct {
    recorded_call *calls;
    int count;
    unsigned char *data;
    size_t data_used;
    int overflowed;
    uint32_t occurrence_keys[OCCURRENCE_SLOTS];
    uint16_t occurrence_counts[OCCURRENCE_SLOTS];
} frame_recording;

static frame_recording *recordings[2];
static int current_recording;
static int has_previous;
static int present_vsync = 1;
static int update_rate = AUTHORED_UPDATE_RATE;
static int replaying;
static int game_loop_running;
static LARGE_INTEGER counter_frequency;
static int max_frames_per_update = 4;
static int display_hz;
static int interpolated_frames;
static int updates_since_report;
static int trace_remaining;
static matrix_fn get_matrix, load_matrix;
static void ***d3dx_interface;
static volatile int *frame_skip_count;
static set_rate_fn original_set_refresh_rate;
static unsigned char *scratch;
static LARGE_INTEGER last_final_present;
static HANDLE pacing_timer;
static int capture_mode, capture_frame, capture_ready, frozen;

static uint32_t occurrence_hash(uint32_t function, uint32_t key) {
    return (function * 2654435761u) ^ (key * 2246822519u);
}

static uint16_t next_occurrence(frame_recording *recording, uint32_t function, uint32_t key) {
    uint32_t hash = occurrence_hash(function, key);
    uint32_t tag = hash | 1;
    for (uint32_t probe = 0; probe < OCCURRENCE_SLOTS; probe++) {
        uint32_t slot = (hash + probe) % OCCURRENCE_SLOTS;
        if (recording->occurrence_keys[slot] == tag) return ++recording->occurrence_counts[slot];
        if (!recording->occurrence_keys[slot]) {
            recording->occurrence_keys[slot] = tag;
            recording->occurrence_counts[slot] = 0;
            return 0;
        }
    }
    return 0;
}

static void reset_recording(frame_recording *recording) {
    recording->count = 0;
    recording->data_used = 0;
    recording->overflowed = 0;
    memset(recording->occurrence_keys, 0, sizeof recording->occurrence_keys);
}

static size_t payload_size(int function, const uint32_t *args) {
    switch (functions[function].payload) {
    case PAYLOAD_SPRITE: return SPRITE_BYTES;
    case PAYLOAD_LIGHT_VECTOR: return LIGHT_VECTOR_BYTES;
    case PAYLOAD_TLV_STRIP: return (size_t)args[1] * TLV_BYTES_PER_VERTEX;
    default: return 0;
    }
}

static uint32_t match_key(int function, const recorded_call *call, const unsigned char *data) {
    if (functions[function].payload == PAYLOAD_TLV_STRIP) return call->args[1];
    if (functions[function].payload == PAYLOAD_SPRITE) return call->args[1];
    (void)data;
    return call->args[0];
}

static void record_call(int function, const uint32_t *args) {
    frame_recording *recording = recordings[current_recording];
    if (recording->count == MAX_CALLS) {
        recording->overflowed = 1;
        return;
    }
    recorded_call *call = &recording->calls[recording->count];
    call->function = (uint16_t)function;
    memcpy(call->args, args, sizeof call->args);
    get_matrix(call->matrix);
    size_t size = payload_size(function, args);
    call->data_size = 0;
    if (size && args[0]) {
        if (recording->data_used + size > DATA_POOL_BYTES) {
            recording->overflowed = 1;
            return;
        }
        memcpy(recording->data + recording->data_used, (const void *)(uintptr_t)args[0], size);
        call->data_offset = (uint32_t)recording->data_used;
        call->data_size = (uint32_t)size;
        recording->data_used += (size + 3) & ~(size_t)3;
    }
    call->occurrence = next_occurrence(recording, function, match_key(function, call, NULL));
    recording->count++;
}

static const recorded_call *find_previous(const recorded_call *call) {
    if (!has_previous) return NULL;
    const frame_recording *previous = recordings[1 - current_recording];
    uint32_t key = match_key(call->function, call, NULL);
    for (int i = 0; i < previous->count; i++) {
        const recorded_call *candidate = &previous->calls[i];
        if (candidate->function == call->function && candidate->occurrence == call->occurrence &&
            match_key(candidate->function, candidate, NULL) == key) {
            return candidate;
        }
    }
    return NULL;
}

static int matrices_close(const float *a, const float *b) {
    for (int i = 0; i < MATRIX_TRANSLATION; i++) {
        if (fabsf(a[i] - b[i]) > MATRIX_JUMP_ROTATION) return 0;
    }
    for (int i = MATRIX_TRANSLATION; i < MATRIX_TRANSLATION + 3; i++) {
        if (fabsf(a[i] - b[i]) > MATRIX_JUMP_TRANSLATION) return 0;
    }
    return 1;
}

static void blend_floats(float *out, const float *from, const float *to, int count, float alpha) {
    for (int i = 0; i < count; i++) out[i] = from[i] + (to[i] - from[i]) * alpha;
}

static void blend_sprite(unsigned char *sprite, const unsigned char *from, float alpha) {
    static const int fields[] = {0x04, 0x08, 0x10, 0x14};
    for (int i = 0; i < 4; i++) {
        float *to = (float *)(sprite + fields[i]);
        float start = *(const float *)(from + fields[i]);
        if (fabsf(*to - start) < SPRITE_JUMP_PIXELS) *to = start + (*to - start) * alpha;
    }
}

static void blend_strip(float *vertices, const float *from, uint32_t count, float alpha) {
    for (uint32_t v = 0; v < count; v++) {
        for (int axis = 0; axis < 2; axis++) {
            float *to = &vertices[v * TLV_FLOATS_PER_VERTEX + axis];
            float start = from[v * TLV_FLOATS_PER_VERTEX + axis];
            if (fabsf(*to - start) < SPRITE_JUMP_PIXELS) *to = start + (*to - start) * alpha;
        }
    }
}

static void replay_call(const frame_recording *recording, const recorded_call *call, float alpha) {
    const recorded_function *function = &functions[call->function];
    const recorded_call *previous = alpha < 1.0f ? find_previous(call) : NULL;
    float matrix[MATRIX_FLOATS];
    memcpy(matrix, call->matrix, sizeof matrix);
    if (previous && function->blend == BLEND_MATRIX && matrices_close(previous->matrix, call->matrix)) {
        blend_floats(matrix, previous->matrix, call->matrix, MATRIX_FLOATS, alpha);
    }
    load_matrix(matrix);
    uint32_t args[4];
    memcpy(args, call->args, sizeof args);
    if (call->data_size) {
        memcpy(scratch, recording->data + call->data_offset, call->data_size);
        const unsigned char *previous_data =
            previous && previous->data_size == call->data_size ? recordings[1 - current_recording]->data + previous->data_offset : NULL;
        if (previous_data && function->blend == BLEND_SPRITE) blend_sprite(scratch, previous_data, alpha);
        if (previous_data && function->blend == BLEND_STRIP) {
            blend_strip((float *)scratch, (const float *)previous_data, call->args[1], alpha);
        }
        args[0] = (uint32_t)(uintptr_t)scratch;
    }
    function->original(args[0], args[1], args[2], args[3]);
}

static int skip_2d_calls, photo_test_skip_all;
#define FIRST_SCREEN_LAYER 0x0e

static int is_screen_space(const float *m) {
    for (int row = 0; row < 3; row++) {
        int nonzero = 0;
        for (int column = 0; column < 3; column++) {
            float value = fabsf(m[row * 4 + column]);
            nonzero += value > 1e-5f;
            if (value > 0.5f) return 0;
        }
        if (nonzero > 1) return 0;
    }
    return m[14] > -20.0f;
}

static int is_set_layer(int function) {
    return !strcmp(functions[function].name, "nlObjPutSetLayer");
}

static void replay_recording_from(const frame_recording *recording, float alpha) {
    replaying = 1;
    uint32_t layer = 0;
    for (int i = 0; i < recording->count; i++) {
        const recorded_call *call = &recording->calls[i];
        int payload = functions[call->function].payload;
        if (is_set_layer(call->function)) layer = call->args[0];
        if (skip_2d_calls) {
            if (photo_test_skip_all && functions[call->function].kind == CALL_HELD && functions[call->function].blend == BLEND_MATRIX) continue;
            if (payload == PAYLOAD_SPRITE || payload == PAYLOAD_TLV_STRIP) continue;
            if (functions[call->function].kind == CALL_HELD && functions[call->function].blend == BLEND_MATRIX &&
                (layer >= FIRST_SCREEN_LAYER || is_screen_space(call->matrix)))
                continue;
        }
        replay_call(recording, call, alpha);
    }
    replaying = 0;
}

static void replay_recording(float alpha) {
    replay_recording_from(recordings[current_recording], alpha);
}

static const frame_recording *frozen_recording(void) {
    if (!recordings[0]) return NULL;
    if (capture_mode) return capture_ready && !recordings[0]->overflowed ? recordings[0] : NULL;
    return has_previous ? recordings[1 - current_recording] : NULL;
}

void frame_interp_capture_next_frame(void) {
    if (!capture_mode) return;
    reset_recording(recordings[0]);
    capture_ready = 0;
    capture_frame = 1;
}

int frame_interp_frame_ready(void) {
    return frozen_recording() != NULL;
}

void frame_interp_set_frozen(int on) {
    frozen = on;
}

void frame_interp_log_frozen(void) {
    const frame_recording *recording = frozen_recording();
    if (!recording) return;
    int run_start = 0;
    for (int i = 1; i <= recording->count; i++) {
        const recorded_call *previous = &recording->calls[i - 1];
        int same = i < recording->count && recording->calls[i].function == previous->function &&
                   functions[previous->function].kind == CALL_HELD;
        if (same) continue;
        log_line("frozen %4d-%4d %-28s x%-4d args %08X %08X %08X", run_start, i - 1, functions[previous->function].name,
                 i - run_start, previous->args[0], previous->args[1], previous->args[2]);
        run_start = i;
    }
}

void frame_interp_log_frozen_matrices(void) {
    const frame_recording *recording = frozen_recording();
    if (!recording) return;
    for (int i = 0; i < recording->count; i++) {
        const recorded_call *call = &recording->calls[i];
        if (functions[call->function].blend != BLEND_MATRIX) continue;
        const float *m = call->matrix;
        log_line("matrix %4d %08X  %7.3f %7.3f %7.3f | %7.3f %7.3f %7.3f | %7.3f %7.3f %7.3f | t %8.2f %8.2f %8.2f", i,
                 call->args[0], m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]);
    }
}

void frame_interp_draw_frozen_scene(void) {
    skip_2d_calls = 1;
    frame_interp_draw_frozen();
    skip_2d_calls = 0;
}

void frame_interp_draw_frozen(void) {
    const frame_recording *recording = frozen_recording();
    if (!recording) return;
    float live_matrix[MATRIX_FLOATS];
    get_matrix(live_matrix);
    replay_recording_from(recording, 1.0f);
    load_matrix(live_matrix);
}

static void present_intermediate(void) {
    void **d3dx = *d3dx_interface;
    if (!d3dx) return;
    subtitle_draw_overlay();
    typedef HRESULT(__stdcall * update_frame_fn)(void *self, DWORD flags);
    update_frame_fn update_frame = ((update_frame_fn *)*d3dx)[D3DX_UPDATE_FRAME_SLOT];
    update_frame(d3dx, present_vsync ? 0 : D3DX_UPDATE_NOVSYNC);
    timing_count_presented_frame();
}

static void wait_until(LONGLONG target) {
    for (;;) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        LONGLONG remaining = target - now.QuadPart;
        if (remaining <= 0) return;
        double remaining_ms = (double)remaining * 1000.0 / (double)counter_frequency.QuadPart;
        if (remaining_ms > 2.0) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((remaining_ms - 1.5) * 10000.0);
            if (SetWaitableTimer(pacing_timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(pacing_timer, INFINITE);
        } else {
            YieldProcessor();
        }
    }
}

static LONGLONG next_display_time(LONGLONG start, LONGLONG now, LONGLONG refresh_ticks) {
    LONGLONG elapsed = now - start;
    return start + (elapsed / refresh_ticks + 1) * refresh_ticks;
}

static int present_interpolated_frames(void) {
    LONGLONG update_ticks = counter_frequency.QuadPart / update_rate;
    LONGLONG refresh_ticks = counter_frequency.QuadPart / display_hz;
    LONGLONG start = last_final_present.QuadPart;
    int presented = 0;
    while (presented < max_frames_per_update - 1) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        LONGLONG display_time = next_display_time(start, now.QuadPart, refresh_ticks);
        if (display_time + refresh_ticks / 2 > start + update_ticks) break;
        replay_recording((float)(display_time - start) / (float)update_ticks);
        present_intermediate();
        wait_until(display_time);
        presented++;
    }
    return presented;
}

static int display_refresh_rate(void) {
    DEVMODEA mode = {0};
    mode.dmSize = sizeof mode;
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
        return (int)mode.dmDisplayFrequency;
    }
    return AUTHORED_UPDATE_RATE;
}

static uint32_t finish_update(int function, const uint32_t *args) {
    float live_matrix[MATRIX_FLOATS];
    get_matrix(live_matrix);
    if (!display_hz) {
        display_hz = display_refresh_rate();
        log_line("timing: display refresh %d Hz, up to %d frames per game update", display_hz,
                 max_frames_per_update);
    }
    if (!recordings[current_recording]->overflowed) {
        if (has_previous && last_final_present.QuadPart && !*frame_skip_count) {
            interpolated_frames += present_interpolated_frames();
        }
        replay_recording(1.0f);
    }
    load_matrix(live_matrix);
    uint32_t result = functions[function].original(args[0], args[1], args[2], args[3]);
    QueryPerformanceCounter(&last_final_present);
    if (++updates_since_report == REPORT_UPDATES) {
        log_line("timing: %.2f in-between frames per game update", (double)interpolated_frames / REPORT_UPDATES);
        interpolated_frames = 0;
        updates_since_report = 0;
    }
    has_previous = !recordings[current_recording]->overflowed;
    current_recording = 1 - current_recording;
    reset_recording(recordings[current_recording]);
    return result;
}

static uint32_t intercept(int function, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t args[4] = {a, b, c, d};
    if (trace_remaining > 0 && !replaying) {
        trace_remaining--;
        log_line("trace: %s(%08X, %08X) recorded %d", functions[function].name, a, b, recordings[current_recording]->count);
    }
    if (replaying || frozen) return functions[function].original(a, b, c, d);
    if (capture_mode) {
        if (!capture_frame) return functions[function].original(a, b, c, d);
        if (functions[function].ends_frame) {
            capture_frame = 0;
            capture_ready = 1;
        } else {
            record_call(function, args);
        }
        return functions[function].original(a, b, c, d);
    }
    if (!game_loop_running) {
        game_loop_running = functions[function].ends_frame;
        return functions[function].original(a, b, c, d);
    }
    if (functions[function].ends_frame) return finish_update(function, args);
    record_call(function, args);
    if (functions[function].kind == CALL_HELD) return 1;
    return functions[function].original(a, b, c, d);
}

#define DEFINE_INTERCEPTOR(index)                                                              \
    static uint32_t __cdecl interceptor_##index(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { \
        return intercept(index, a, b, c, d);                                                   \
    }
DEFINE_INTERCEPTOR(0) DEFINE_INTERCEPTOR(1) DEFINE_INTERCEPTOR(2) DEFINE_INTERCEPTOR(3) DEFINE_INTERCEPTOR(4)
DEFINE_INTERCEPTOR(5) DEFINE_INTERCEPTOR(6) DEFINE_INTERCEPTOR(7) DEFINE_INTERCEPTOR(8) DEFINE_INTERCEPTOR(9)
DEFINE_INTERCEPTOR(10) DEFINE_INTERCEPTOR(11) DEFINE_INTERCEPTOR(12) DEFINE_INTERCEPTOR(13) DEFINE_INTERCEPTOR(14)
DEFINE_INTERCEPTOR(15) DEFINE_INTERCEPTOR(16) DEFINE_INTERCEPTOR(17) DEFINE_INTERCEPTOR(18) DEFINE_INTERCEPTOR(19)
DEFINE_INTERCEPTOR(20) DEFINE_INTERCEPTOR(21) DEFINE_INTERCEPTOR(22) DEFINE_INTERCEPTOR(23) DEFINE_INTERCEPTOR(24)
DEFINE_INTERCEPTOR(25) DEFINE_INTERCEPTOR(26) DEFINE_INTERCEPTOR(27) DEFINE_INTERCEPTOR(28) DEFINE_INTERCEPTOR(29)
DEFINE_INTERCEPTOR(30) DEFINE_INTERCEPTOR(31) DEFINE_INTERCEPTOR(32) DEFINE_INTERCEPTOR(33) DEFINE_INTERCEPTOR(34)
DEFINE_INTERCEPTOR(35) DEFINE_INTERCEPTOR(36)

static void *const interceptors[] = {
    interceptor_0,  interceptor_1,  interceptor_2,  interceptor_3,  interceptor_4,  interceptor_5,  interceptor_6,
    interceptor_7,  interceptor_8,  interceptor_9,  interceptor_10, interceptor_11, interceptor_12, interceptor_13,
    interceptor_14, interceptor_15, interceptor_16, interceptor_17, interceptor_18, interceptor_19, interceptor_20,
    interceptor_21, interceptor_22, interceptor_23, interceptor_24, interceptor_25, interceptor_26, interceptor_27,
    interceptor_28, interceptor_29, interceptor_30, interceptor_31, interceptor_32, interceptor_33, interceptor_34,
    interceptor_35, interceptor_36,
};
_Static_assert(sizeof interceptors / sizeof interceptors[0] == sizeof functions / sizeof functions[0],
               "one interceptor per recorded function");

static int redirect_game_imports(void *target, void *replacement) {
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS32 *headers = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *directory = &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int redirected = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress);
         descriptor->Name; descriptor++) {
        void **slots = (void **)(base + descriptor->FirstThunk);
        for (int i = 0; slots[i]; i++) {
            if (slots[i] != target) continue;
            DWORD old_protection;
            VirtualProtect(&slots[i], sizeof slots[i], PAGE_READWRITE, &old_protection);
            slots[i] = replacement;
            VirtualProtect(&slots[i], sizeof slots[i], old_protection, &old_protection);
            redirected++;
        }
    }
    return redirected;
}

static void present_game_frames_without_vsync(HMODULE hglcore) {
    unsigned char *push_flags = (unsigned char *)hglcore + (VSYNC_PRESENT_FLAGS_VA - HGLCORE_GHIDRA_BASE);
    if (push_flags[0] != 0x6A || push_flags[1] != 0x00) {
        log_line("timing: unexpected bytes in VSync, game frames keep vsync");
        return;
    }
    DWORD old_protection;
    VirtualProtect(push_flags + 1, 1, PAGE_EXECUTE_READWRITE, &old_protection);
    push_flags[1] = D3DX_UPDATE_NOVSYNC;
    VirtualProtect(push_flags + 1, 1, old_protection, &old_protection);
    FlushInstructionCache(GetCurrentProcess(), push_flags, 2);
    log_line("timing: game frames presented without vsync");
}

static int __cdecl set_refresh_rate_hook(unsigned rate) {
    return original_set_refresh_rate(rate == AUTHORED_UPDATE_RATE ? (unsigned)update_rate : rate);
}

static int redirect_all_functions(const char *ini_path) {
    int redirected = 0;
    int first = GetPrivateProfileIntA("debug", "interp_first", 0, ini_path);
    int last = GetPrivateProfileIntA("debug", "interp_last", FUNCTION_COUNT - 1, ini_path);
    for (int i = 0; i < FUNCTION_COUNT; i++) {
        if (i < first || i > last) {
            functions[i].original = (call4_fn)GetProcAddress(GetModuleHandleA(functions[i].module), functions[i].name);
            continue;
        }
        HMODULE module = GetModuleHandleA(functions[i].module);
        void *target = (void *)GetProcAddress(module, functions[i].name);
        if (!target) continue;
        functions[i].original = (call4_fn)target;
        redirected += redirect_game_imports(target, interceptors[i]);
    }
    return redirected;
}

static int allocate_recordings(void) {
    for (int i = 0; i < 2; i++) {
        recordings[i] = VirtualAlloc(NULL, sizeof *recordings[i], MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!recordings[i]) return 0;
        recordings[i]->calls = VirtualAlloc(NULL, MAX_CALLS * sizeof(recorded_call), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        recordings[i]->data = VirtualAlloc(NULL, DATA_POOL_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!recordings[i]->calls || !recordings[i]->data) return 0;
    }
    scratch = VirtualAlloc(NULL, SCRATCH_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return scratch != NULL;
}

void install_frame_interpolation(const char *ini_path) {
    int interpolation = GetPrivateProfileIntA("timing", "frame_interpolation", 0, ini_path);
    max_frames_per_update = GetPrivateProfileIntA("timing", "max_frames_per_update", 4, ini_path);
    display_hz = GetPrivateProfileIntA("timing", "display_hz", 0, ini_path);
    trace_remaining = GetPrivateProfileIntA("debug", "trace_frames", 0, ini_path);
    update_rate = GetPrivateProfileIntA("timing", "update_rate", AUTHORED_UPDATE_RATE, ini_path);
    present_vsync = GetPrivateProfileIntA("timing", "present_vsync", 1, ini_path);
    if (max_frames_per_update < 2) max_frames_per_update = 2;
    if (max_frames_per_update > 8) max_frames_per_update = 8;
    if (update_rate < 10 || update_rate > 240) update_rate = AUTHORED_UPDATE_RATE;
    int speed = GetPrivateProfileIntA("mutators", "speed", 100, ini_path);
    if (update_rate == AUTHORED_UPDATE_RATE && speed >= 50 && speed <= 200 && speed != 100) {
        update_rate = AUTHORED_UPDATE_RATE * speed / 100;
        log_line("mutators: game speed %d%%", speed);
    }
    HMODULE hglcore = GetModuleHandleA("HglCore.dll"), hglna = GetModuleHandleA("HglNa.dll");
    if (!hglcore || !hglna) return;
    if (update_rate != AUTHORED_UPDATE_RATE) {
        void *target = (void *)GetProcAddress(hglcore, "hlSetRefreshRate");
        MH_STATUS status = target ? MH_CreateHook(target, set_refresh_rate_hook, (void **)&original_set_refresh_rate)
                                  : MH_ERROR_FUNCTION_NOT_FOUND;
        if (status == MH_OK) status = MH_EnableHook(target);
        log_line("timing: game updates per second %d (hook hlSetRefreshRate: %s)", update_rate, MH_StatusToString(status));
    }
    if (!present_vsync) present_game_frames_without_vsync(hglcore);
    get_matrix = (matrix_fn)GetProcAddress(hglna, "nlGetMatrix");
    load_matrix = (matrix_fn)GetProcAddress(hglna, "nlLoadMatrix");
    if (!interpolation) {
        if (!GetPrivateProfileIntA("gameplay", "pause_menu", 1, ini_path) && !GetPrivateProfileIntA("fun", "photo_mode", 0, ini_path)) return;
        if (!get_matrix || !load_matrix || !allocate_recordings()) {
            log_line("pause: frame capture unavailable");
            return;
        }
        capture_mode = 1;
        photo_test_skip_all = GetPrivateProfileIntA("debug", "photo_test_skip_all", 0, ini_path);
        log_line("pause: frame capture ready (%d game imports redirected)", redirect_all_functions(ini_path));
        return;
    }
    d3dx_interface = (void ***)GetProcAddress(hglcore, "g_lpD3DX");
    frame_skip_count = (volatile int *)GetProcAddress(hglcore, "hlDelayCount");
    QueryPerformanceFrequency(&counter_frequency);
    pacing_timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002, TIMER_ALL_ACCESS);
    if (!pacing_timer) pacing_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (!get_matrix || !load_matrix || !d3dx_interface || !frame_skip_count || !pacing_timer || !allocate_recordings()) {
        log_line("timing: frame interpolation unavailable");
        return;
    }
    int redirected = redirect_all_functions(ini_path);
    log_line("timing: frame interpolation on (%d game updates/s, present vsync %d); %d game imports redirected",
             update_rate, present_vsync, redirected);
}
