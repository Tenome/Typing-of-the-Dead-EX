#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "overlay_hooks.h"
#include "display_hooks.h"
#include "log.h"
#include "MinHook.h"

#define MODEL_TABLE_VA 0x009f3ac4
#define MODEL_TABLE_STRIDE_BYTES 16
#define MODEL_TABLE_ENTRIES 0x24b4
#define MATRIX_FLOATS 16
#define MATRIX_TX 12
#define MATRIX_TY 13
#define MATRIX_TZ 14
#define MAX_LOGGED_OBJECTS 1024
#define MAX_WIDENED_MODELS 16
#define OBJECT_PUT_VARIANTS 5

typedef void(__cdecl *object_put_fn)(void *object, int a, int b, int c);
typedef void(__cdecl *matrix_fn)(float *matrix);

typedef struct {
    const char *name;
    object_put_fn original;
} object_put_hook;

static object_put_hook object_put_hooks[OBJECT_PUT_VARIANTS] = {
    {"nlObjPut"}, {"nlObjPutTrnsl"}, {"nlObjPutML"}, {"nlObjPutMLTrnsl"}, {"nlObjPutCheapEnvMap"}};
static matrix_fn get_matrix, load_matrix;
static void *(*object_drawn_callback)(void *object);

void overlay_set_object_drawn_callback(void *(*callback)(void *object)) {
    object_drawn_callback = callback;
}
static int log_objects;
static void *logged_objects[MAX_LOGGED_OBJECTS];
static int logged_object_count;
static int widened_models[MAX_WIDENED_MODELS];
static int widened_model_count;

static void *model_object(int id) {
    return *(void **)((unsigned char *)MODEL_TABLE_VA + id * MODEL_TABLE_STRIDE_BYTES);
}

static int model_id_for_object(void *object) {
    for (int id = 1; id < MODEL_TABLE_ENTRIES; id++) {
        if (model_object(id) == object) return id;
    }
    return -1;
}

static int is_widened_model(void *object) {
    for (int i = 0; i < widened_model_count; i++) {
        if (model_object(widened_models[i]) == object) return 1;
    }
    return 0;
}

static void log_object_once(int which, void *object, void *caller) {
    if (logged_object_count == MAX_LOGGED_OBJECTS) return;
    for (int i = 0; i < logged_object_count; i++) {
        if (logged_objects[i] == object) return;
    }
    logged_objects[logged_object_count++] = object;
    float m[MATRIX_FLOATS];
    get_matrix(m);
    log_line("object: %s %p model %d from %p, translation %.3f %.3f %.3f, axes x(%.3f %.3f %.3f) y(%.3f %.3f %.3f)",
             object_put_hooks[which].name, object, model_id_for_object(object), caller, m[MATRIX_TX], m[MATRIX_TY],
             m[MATRIX_TZ], m[0], m[1], m[2], m[4], m[5], m[6]);
}

static void put_object(int which, void *object, int a, int b, int c, void *caller) {
    if (log_objects) log_object_once(which, object, caller);
    float factor = widened_model_count ? display_widescreen_factor() : 1.0f;
    float fov_factor = widened_model_count ? display_fov_factor() : 1.0f;
    if ((factor == 1.0f && fov_factor == 1.0f) || !is_widened_model(object)) {
        object_put_hooks[which].original(object, a, b, c);
        void *extra = object_drawn_callback ? object_drawn_callback(object) : NULL;
        if (extra) object_put_hooks[which].original(extra, a, b, c);
        return;
    }
    float saved[MATRIX_FLOATS], widened[MATRIX_FLOATS];
    get_matrix(saved);
    memcpy(widened, saved, sizeof widened);
    for (int i = 0; i < 3; i++) {
        widened[i] *= factor * fov_factor;
        widened[4 + i] *= fov_factor;
    }
    widened[MATRIX_TX] *= factor * fov_factor;
    widened[MATRIX_TY] *= fov_factor;
    load_matrix(widened);
    object_put_hooks[which].original(object, a, b, c);
    load_matrix(saved);
}

#define DEFINE_OBJECT_PUT_HOOK(index)                                                \
    static void __cdecl object_put_hook_##index(void *object, int a, int b, int c) { \
        put_object(index, object, a, b, c, __builtin_return_address(0));             \
    }
DEFINE_OBJECT_PUT_HOOK(0)
DEFINE_OBJECT_PUT_HOOK(1)
DEFINE_OBJECT_PUT_HOOK(2)
DEFINE_OBJECT_PUT_HOOK(3)
DEFINE_OBJECT_PUT_HOOK(4)

static void read_widened_models(const char *ini_path) {
    char list[256];
    GetPrivateProfileStringA("display", "widen_models", "2421", list, sizeof list, ini_path);
    for (char *item = strtok(list, ", "); item && widened_model_count < MAX_WIDENED_MODELS; item = strtok(NULL, ", ")) {
        int id = atoi(item);
        if (id > 0 && id < MODEL_TABLE_ENTRIES) widened_models[widened_model_count++] = id;
    }
}

void install_overlay_hooks(const char *ini_path) {
    log_objects = GetPrivateProfileIntA("display", "log_objects", 0, ini_path);
    read_widened_models(ini_path);
    if (!log_objects && !widened_model_count) return;
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    if (!hglna) return;
    get_matrix = (matrix_fn)GetProcAddress(hglna, "nlGetMatrix");
    load_matrix = (matrix_fn)GetProcAddress(hglna, "nlLoadMatrix");
    log_line("display: widening %d camera-attached model(s) for widescreen", widened_model_count);
    void *hooks[OBJECT_PUT_VARIANTS] = {object_put_hook_0, object_put_hook_1, object_put_hook_2, object_put_hook_3,
                                        object_put_hook_4};
    for (int i = 0; i < OBJECT_PUT_VARIANTS; i++) {
        void *target = (void *)GetProcAddress(hglna, object_put_hooks[i].name);
        MH_STATUS status = target ? MH_CreateHook(target, hooks[i], (void **)&object_put_hooks[i].original)
                                  : MH_ERROR_FUNCTION_NOT_FOUND;
        if (status == MH_OK) status = MH_EnableHook(target);
        log_line("hook %s at %p: %s", object_put_hooks[i].name, target, MH_StatusToString(status));
    }
}
