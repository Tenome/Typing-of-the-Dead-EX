#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "skinned_models.h"
#include "model_hooks.h"
#include "log.h"
#include "MinHook.h"

#define DRAW_MODEL_VA 0x0048b400
#define DRAW_MODEL_SCALED_VA 0x0048b460
#define CURRENT_CHARACTER (*(unsigned char *const *)0x00a3e304)
#define CHARACTER_SCALE_OFFSET 0x116c
#define MAX_MODEL_ID 0x24b3
#define MAX_SKINS 16
#define MAX_PARTS 16
#define MAX_INFLUENCES 4
#define CHEST_PART 1
#define OBJECT_HEADER_WORDS 6
#define OBJECT_SPHERE_WORD 2
#define CHUNK_HEADER_WORDS 20
#define CHUNK_SPHERE_WORD 4
#define CHUNK_SIZE_WORD 19
#define VERTEX_FLOATS 8
#define STRIP_LIST_FLAG 0x08
#define COPIES 32
#define HGLNA_OBJ_PUT_ORDINAL 110

typedef struct {
    unsigned char part;
    float weight;
    float position[3];
    float normal[3];
} influence;

typedef struct {
    unsigned char count;
    influence influences[MAX_INFLUENCES];
} skin_corner;

typedef struct {
    int kind, carrier;
    unsigned required_parts;
    skin_corner *corners;
    int corner_count;
    const unsigned char *character;
    unsigned captured_parts;
    float matrices[MAX_PARTS][16];
    float scale;
    uint32_t *copies[COPIES];
    size_t copy_bytes;
    int next_copy;
    int drawn, incomplete, mismatch_logged;
} skin;

typedef void(__cdecl *draw_model_fn)(int model_id);
typedef void(__cdecl *matrix_fn)(float *matrix);
typedef void(__cdecl *object_put_fn)(uint32_t *object);

static draw_model_fn original_draw_model, original_draw_model_scaled;
static matrix_fn get_matrix, load_matrix;
static object_put_fn *object_put_slot;
static skin skins[MAX_SKINS];
static int skin_count;
static signed char skin_of_model[MAX_MODEL_ID + 1];
static signed char part_of_model[MAX_MODEL_ID + 1];

static int read_skin(const char *path, skin *out) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    char line[1024];
    int version = 0, ok = 1, corner = 0;
    memset(out, 0, sizeof *out);
    out->carrier = -1;
    while (ok && fgets(line, sizeof line, file)) {
        int a, b;
        if (sscanf(line, "todmod skin %d", &version) == 1) continue;
        if (sscanf(line, "kind %d", &a) == 1) {
            out->kind = a;
        } else if (sscanf(line, "carrier %d", &a) == 1) {
            out->carrier = a;
        } else if (sscanf(line, "model %d %d", &a, &b) == 2) {
            if (a < 0 || a > MAX_MODEL_ID || b < 0 || b >= MAX_PARTS) continue;
            part_of_model[a] = (signed char)b;
            skin_of_model[a] = (signed char)skin_count;
            out->required_parts |= 1u << b;
        } else if (sscanf(line, "corners %d", &a) == 1) {
            out->corners = calloc((size_t)a, sizeof *out->corners);
            out->corner_count = out->corners ? a : 0;
            ok = out->corners != NULL;
        } else if (line[0] == 'c' && line[1] == ' ' && corner < out->corner_count) {
            skin_corner *c = &out->corners[corner++];
            char *cursor = line + 2;
            int count = (int)strtol(cursor, &cursor, 10);
            if (count < 1 || count > MAX_INFLUENCES) {
                ok = 0;
                break;
            }
            c->count = (unsigned char)count;
            for (int i = 0; i < count; i++) {
                influence *in = &c->influences[i];
                in->part = (unsigned char)strtol(cursor, &cursor, 10);
                in->weight = strtof(cursor, &cursor);
                for (int k = 0; k < 3; k++) in->position[k] = strtof(cursor, &cursor);
                for (int k = 0; k < 3; k++) in->normal[k] = strtof(cursor, &cursor);
                if (in->part >= MAX_PARTS) ok = 0;
            }
        }
    }
    fclose(file);
    if (!ok || version != 1 || out->carrier < 0 || corner != out->corner_count || !(out->required_parts & (1u << CHEST_PART))) {
        free(out->corners);
        out->corners = NULL;
        for (int id = 0; id <= MAX_MODEL_ID; id++) {
            if (skin_of_model[id] == skin_count) skin_of_model[id] = part_of_model[id] = -1;
        }
        log_line("skins: %s could not be read", path);
        return 0;
    }
    return 1;
}

static void find_skins(const char *folder) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s*", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (found.cFileName[0] == '.') continue;
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s%s", folder, found.cFileName);
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            strncat(path, "\\", sizeof path - strlen(path) - 1);
            find_skins(path);
            continue;
        }
        const char *dot = strrchr(found.cFileName, '.');
        if (!dot || _stricmp(dot, ".skin") || skin_count == MAX_SKINS) continue;
        if (read_skin(path, &skins[skin_count])) {
            log_line("skins: kind %d drawn as one bending mesh (%d corners, carried by model %d) from %s",
                     skins[skin_count].kind, skins[skin_count].corner_count, skins[skin_count].carrier, path);
            skin_count++;
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static void transform_point(const float *m, const float *p, float *out) {
    for (int k = 0; k < 3; k++) out[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + m[12 + k];
}

static void transform_direction(const float *m, const float *d, float *out) {
    for (int k = 0; k < 3; k++) out[k] = d[0] * m[k] + d[1] * m[4 + k] + d[2] * m[8 + k];
}

static int invert_affine(const float *m, float *out) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) return 0;
    float r = 1.0f / det;
    memset(out, 0, 16 * sizeof *out);
    out[0] = (e * i - f * h) * r;
    out[1] = (c * h - b * i) * r;
    out[2] = (b * f - c * e) * r;
    out[4] = (f * g - d * i) * r;
    out[5] = (a * i - c * g) * r;
    out[6] = (c * d - a * f) * r;
    out[8] = (d * h - e * g) * r;
    out[9] = (b * g - a * h) * r;
    out[10] = (a * e - b * d) * r;
    for (int k = 0; k < 3; k++) out[12 + k] = -(m[12] * out[k] + m[13] * out[4 + k] + m[14] * out[8 + k]);
    out[15] = 1.0f;
    return 1;
}

static void normalise(float *v) {
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 1e-12f) for (int k = 0; k < 3; k++) v[k] /= length;
}

static void bound(const float *v, float *low, float *high) {
    for (int k = 0; k < 3; k++) {
        if (v[k] < low[k]) low[k] = v[k];
        if (v[k] > high[k]) high[k] = v[k];
    }
}

static void write_sphere(float *sphere, const float *low, const float *high) {
    float radius = 0;
    for (int k = 0; k < 3; k++) {
        sphere[k] = (low[k] + high[k]) / 2;
        radius += (high[k] - low[k]) * (high[k] - low[k]);
    }
    sphere[3] = sqrtf(radius) / 2;
}

static uint32_t *fresh_copy(skin *s, const uint32_t *source) {
    size_t bytes = model_hooks_object_size(source);
    if (!bytes) return NULL;
    if (bytes != s->copy_bytes) {
        for (int i = 0; i < COPIES; i++) {
            free(s->copies[i]);
            s->copies[i] = malloc(bytes);
            if (!s->copies[i]) return NULL;
        }
        s->copy_bytes = bytes;
    }
    uint32_t *copy = s->copies[s->next_copy];
    s->next_copy = (s->next_copy + 1) % COPIES;
    memcpy(copy, source, bytes);
    return copy;
}

static int bend_mesh(skin *s, uint32_t *object) {
    float to_chest[16];
    if (!invert_affine(s->matrices[CHEST_PART], to_chest)) return 0;
    float inverse_scale = s->scale > 1e-6f ? 1.0f / s->scale : 1.0f;
    uint32_t *cursor = object + OBJECT_HEADER_WORDS;
    float object_low[3] = {1e30f, 1e30f, 1e30f}, object_high[3] = {-1e30f, -1e30f, -1e30f};
    float chunk_low[3], chunk_high[3];
    uint32_t *chunk = NULL;
    int corner = 0;
    while (*cursor) {
        if (*cursor >> 31) {
            if (chunk) write_sphere((float *)(chunk + CHUNK_SPHERE_WORD), chunk_low, chunk_high);
            chunk = cursor;
            for (int k = 0; k < 3; k++) chunk_low[k] = 1e30f, chunk_high[k] = -1e30f;
            cursor += CHUNK_HEADER_WORDS;
            continue;
        }
        uint32_t count = cursor[1] * (cursor[0] & STRIP_LIST_FLAG ? 3 : 1);
        cursor += 2;
        for (uint32_t i = 0; i < count; i++, corner++, cursor += VERTEX_FLOATS) {
            if (corner >= s->corner_count) return 0;
            const skin_corner *c = &s->corners[corner];
            float world[3] = {0}, normal[3] = {0};
            for (int k = 0; k < c->count; k++) {
                const influence *in = &c->influences[k];
                float local[3] = {in->position[0] * s->scale, in->position[1] * s->scale, in->position[2] * s->scale};
                float p[3], n[3];
                transform_point(s->matrices[in->part], local, p);
                transform_direction(s->matrices[in->part], in->normal, n);
                for (int axis = 0; axis < 3; axis++) {
                    world[axis] += in->weight * p[axis];
                    normal[axis] += in->weight * n[axis];
                }
            }
            float *vertex = (float *)cursor;
            float local[3], local_normal[3];
            transform_point(to_chest, world, local);
            transform_direction(to_chest, normal, local_normal);
            normalise(local_normal);
            for (int k = 0; k < 3; k++) {
                vertex[k] = local[k] * inverse_scale;
                vertex[3 + k] = local_normal[k];
            }
            uint32_t bits;
            memcpy(&bits, &vertex[0], 4);
            bits |= 1;
            memcpy(&vertex[0], &bits, 4);
            bound(vertex, chunk_low, chunk_high);
            bound(vertex, object_low, object_high);
        }
    }
    if (chunk) write_sphere((float *)(chunk + CHUNK_SPHERE_WORD), chunk_low, chunk_high);
    write_sphere((float *)(object + OBJECT_SPHERE_WORD), object_low, object_high);
    return corner == s->corner_count;
}

static void draw_skin(skin *s) {
    uint32_t *carrier = model_hooks_replacement(s->carrier);
    uint32_t *object = carrier ? fresh_copy(s, carrier) : NULL;
    if (!object) return;
    if (!bend_mesh(s, object)) {
        if (!s->mismatch_logged++) log_line("skins: kind %d mesh does not match its model %d, not drawn", s->kind, s->carrier);
        return;
    }
    float saved[16];
    get_matrix(saved);
    load_matrix(s->matrices[CHEST_PART]);
    (*object_put_slot)(object);
    load_matrix(saved);
    if (!s->drawn++) log_line("skins: kind %d drawn as one bending mesh", s->kind);
}

static int capture_part(int model_id) {
    if (model_id < 0 || model_id > MAX_MODEL_ID || skin_of_model[model_id] < 0) return 0;
    skin *s = &skins[skin_of_model[model_id]];
    int part = part_of_model[model_id];
    const unsigned char *character = CURRENT_CHARACTER;
    if (character != s->character || s->captured_parts & (1u << part)) {
        if (s->captured_parts && s->captured_parts != s->required_parts) s->incomplete++;
        s->character = character;
        s->captured_parts = 0;
    }
    get_matrix(s->matrices[part]);
    if (part == CHEST_PART) {
        s->scale = character ? *(const float *)(character + CHARACTER_SCALE_OFFSET) : 1.0f;
        if (!(s->scale > 0.01f && s->scale < 100.0f)) s->scale = 1.0f;
    }
    s->captured_parts |= 1u << part;
    if (s->captured_parts == s->required_parts) {
        draw_skin(s);
        s->captured_parts = 0;
    }
    return 1;
}

static void __cdecl draw_model_hook(int model_id) {
    if (capture_part(model_id)) return;
    original_draw_model(model_id);
}

static void __cdecl draw_model_scaled_hook(int model_id) {
    if (capture_part(model_id)) return;
    original_draw_model_scaled(model_id);
}

static object_put_fn *find_game_import(const char *module, int ordinal) {
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS32 *headers = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *directory = &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress); d->Name; d++) {
        if (_stricmp((const char *)(base + d->Name), module)) continue;
        IMAGE_THUNK_DATA32 *names = (IMAGE_THUNK_DATA32 *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        void **slots = (void **)(base + d->FirstThunk);
        for (int i = 0; names[i].u1.Ordinal; i++) {
            if (IMAGE_SNAP_BY_ORDINAL32(names[i].u1.Ordinal) && IMAGE_ORDINAL32(names[i].u1.Ordinal) == (DWORD)ordinal) {
                return (object_put_fn *)&slots[i];
            }
        }
    }
    return NULL;
}

void install_skinned_models(void) {
    memset(skin_of_model, -1, sizeof skin_of_model);
    memset(part_of_model, -1, sizeof part_of_model);
    char folder[MAX_PATH];
    for (int i = 0; model_hooks_pack_folder(i, folder, sizeof folder); i++) find_skins(folder);
    if (!skin_count) return;
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    get_matrix = hglna ? (matrix_fn)GetProcAddress(hglna, "nlGetMatrix") : NULL;
    load_matrix = hglna ? (matrix_fn)GetProcAddress(hglna, "nlLoadMatrix") : NULL;
    object_put_slot = find_game_import("HGLNA.DLL", HGLNA_OBJ_PUT_ORDINAL);
    if (!get_matrix || !load_matrix || !object_put_slot) {
        log_line("skins: HglNa matrix or object functions not found, bending meshes off");
        return;
    }
    MH_STATUS status = MH_CreateHook((void *)DRAW_MODEL_VA, draw_model_hook, (void **)&original_draw_model);
    if (status == MH_OK) status = MH_EnableHook((void *)DRAW_MODEL_VA);
    MH_STATUS scaled = MH_CreateHook((void *)DRAW_MODEL_SCALED_VA, draw_model_scaled_hook, (void **)&original_draw_model_scaled);
    if (scaled == MH_OK) scaled = MH_EnableHook((void *)DRAW_MODEL_SCALED_VA);
    log_line("skins: %d bending meshes (hooks draw_model %s, draw_model_scaled %s)", skin_count, MH_StatusToString(status),
             MH_StatusToString(scaled));
}
