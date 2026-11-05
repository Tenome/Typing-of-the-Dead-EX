#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "model_hooks.h"
#include "log.h"
#include "MinHook.h"

#define FINISH_SINGLE_MODEL_VA 0x0048c5c0
#define FINISH_BANK_MODEL_VA 0x0048c680
#define LOAD_OBJECT_TEXTURES_VA 0x10006f40
#define HGLNA_PREFERRED_BASE 0x10000000
#define MODEL_TABLE ((model_entry *)0x009f3ac0)
#define BANK_LOAD_NEXT_MODEL_ID (*(const short **)0x007586ec)
#define LOADING_MODEL_HAS_TEXTURES (*(const int *)0x0075871c)
#define REQUEST_MODEL_ID 2
#define MAX_MODEL_ID 0x24b3
#define MODEL_LOADED_FLAG 0x8000
#define NO_BANK 0x102
#define TEXTURE_HEADER_ID_WORD 3

#define OBJECT_HEADER_SIZE 0x18
#define CHUNK_HEADER_SIZE 0x50
#define CHUNK_HEADER_WORDS (CHUNK_HEADER_SIZE / 4)
#define CHUNK_SPHERE_WORD 4
#define CHUNK_TEXTURE_WORD 8
#define CHUNK_SIZE_WORD 19
#define VERTEX_FLOATS 8
#define STRIP_LIST_FLAG 0x08
#define STRIP_REFERENCES_FLAG 0x20
#define NO_TEXTURE 0xFFFFFFFFu
#define MAX_TRIANGLES_PER_LIST 1000
#define MAX_OBJECT_BYTES 0x1000000
#define MAX_PACKS 8
#define SETUP_BELT_VA 0x00493f50
#define CURRENT_CHARACTER (*(unsigned char *const *)0x00a3e304)
#define CHARACTER_KIND_OFFSET 0x60
#define CHARACTER_BELTS_OFFSET 0x40
#define BELT_SLOT_STRIDE 8
#define MAX_CHARACTER_KIND 0x80
#define PACK_NAME_LENGTH 64
#define BANK_NAME_LENGTH 64

typedef struct {
    void *buffer;
    uint32_t *object;
    short previous;
    short next;
    unsigned short flags;
    short unused;
} model_entry;

typedef int(__cdecl *finish_model_fn)(int *request);
typedef int(__cdecl *setup_belt_fn)(int part, int vertices);
typedef int(__cdecl *load_object_textures_fn)(uint32_t *object, void *headers, void *data);

static finish_model_fn original_finish_single_model;
static finish_model_fn original_finish_bank_model;
static load_object_textures_fn original_load_object_textures;
static setup_belt_fn original_setup_belt;
static const int *const *belt_database;
static int belts_skipped;

static char models_folder[MAX_PATH];
static char pack_names[MAX_PACKS][PACK_NAME_LENGTH];
static int pack_count;
static int dump_models;
static FILE *model_log;
static const unsigned short *bank_of_model;
static const char *const *bank_file_names;
static const uint32_t *const *bank_texture_headers;
static int loading_model_id = -1;
static uint32_t *replacement_objects[MAX_MODEL_ID + 1];
static unsigned char model_seen[MAX_MODEL_ID + 1];

typedef struct {
    float *positions;
    float *uvs;
    float *normals;
    int position_count, uv_count, normal_count;
    int *corners;
    int *corner_materials;
    int corner_count;
    char (*material_names)[64];
    int material_count;
} obj_mesh;

static void model_bank_name(int model_id, char *out, size_t size) {
    unsigned bank = bank_of_model ? bank_of_model[model_id] : NO_BANK;
    const char *name = bank != NO_BANK && bank_file_names && bank_file_names[bank] ? bank_file_names[bank] : "unknown";
    lstrcpynA(out, name, (int)size);
    char *dot = strrchr(out, '.');
    if (dot && !lstrcmpiA(dot, ".bin")) *dot = '\0';
}

static const uint32_t *resolve_vertex(const uint32_t *cursor, int references, const uint32_t **next) {
    if (references && !(cursor[0] & 1)) {
        *next = cursor + 2;
        return (const uint32_t *)((const char *)(cursor + 2) + (int32_t)cursor[1]);
    }
    *next = cursor + VERTEX_FLOATS;
    return cursor;
}

static size_t object_size(const uint32_t *object, size_t limit) {
    const uint32_t *cursor = object + OBJECT_HEADER_SIZE / 4;
    const uint32_t *end = (const uint32_t *)((const char *)object + (limit & ~(size_t)3));
    while (cursor < end && *cursor) {
        if (*cursor >> 31) {
            cursor += CHUNK_HEADER_WORDS;
            continue;
        }
        if (cursor + 2 > end) return 0;
        uint32_t flags = cursor[0], count = cursor[1];
        if (flags & STRIP_LIST_FLAG) count *= 3;
        cursor += 2;
        for (uint32_t i = 0; i < count && cursor < end; i++) resolve_vertex(cursor, flags & STRIP_REFERENCES_FLAG, &cursor);
    }
    return cursor < end ? (size_t)((const char *)(cursor + 1) - (const char *)object) : 0;
}

static void write_object_obj(const uint32_t *object, const char *path, const char *mtl_name) {
    FILE *obj = fopen(path, "w");
    if (!obj) return;
    fprintf(obj, "# Typing of the Dead EX model (right-handed: z negated)\nmtllib %s\n", mtl_name);
    const uint32_t *cursor = object + OBJECT_HEADER_SIZE / 4;
    int written = 0, chunk_index = -1;
    while (*cursor) {
        if (*cursor >> 31) {
            chunk_index++;
            uint32_t texture = cursor[CHUNK_TEXTURE_WORD];
            if (texture == NO_TEXTURE) fprintf(obj, "usemtl m%d_none\n", chunk_index);
            else fprintf(obj, "usemtl m%d_t%u\n", chunk_index, texture & 0x3FFFFFFF);
            cursor += CHUNK_HEADER_WORDS;
            continue;
        }
        uint32_t flags = cursor[0], count = cursor[1];
        int is_list = (flags & STRIP_LIST_FLAG) != 0;
        if (is_list) count *= 3;
        cursor += 2;
        int first = written + 1;
        for (uint32_t i = 0; i < count; i++) {
            const float *vertex = (const float *)resolve_vertex(cursor, flags & STRIP_REFERENCES_FLAG, &cursor);
            fprintf(obj, "v %.6f %.6f %.6f\nvn %.6f %.6f %.6f\nvt %.6f %.6f\n", vertex[0], vertex[1], -vertex[2],
                    vertex[3], vertex[4], -vertex[5], vertex[6], 1.0f - vertex[7]);
            written++;
        }
        for (uint32_t i = 0; i + 2 < count; i += is_list ? 3 : 1) {
            int a = first + (int)i, b = a + 1, c = a + 2;
            if (!is_list && (i & 1)) { int swap = a; a = b; b = swap; }
            fprintf(obj, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a, a, b, b, b, c, c, c);
        }
    }
    fclose(obj);
}

static uint32_t bank_texture_id(int model_id, uint32_t texture_index) {
    unsigned bank = bank_of_model ? bank_of_model[model_id] : NO_BANK;
    if (bank == NO_BANK || !bank_texture_headers || !bank_texture_headers[bank]) return NO_TEXTURE;
    return bank_texture_headers[bank][texture_index * 4 + TEXTURE_HEADER_ID_WORD];
}

static void write_object_mtl(int model_id, const uint32_t *object, const char *path) {
    FILE *mtl = fopen(path, "w");
    if (!mtl) return;
    const uint32_t *cursor = object + OBJECT_HEADER_SIZE / 4;
    int chunk_index = 0;
    while (*cursor >> 31) {
        uint32_t texture = cursor[CHUNK_TEXTURE_WORD] & 0x3FFFFFFF;
        const float *colours = (const float *)cursor;
        if (cursor[CHUNK_TEXTURE_WORD] == NO_TEXTURE) fprintf(mtl, "newmtl m%d_none\n", chunk_index);
        else fprintf(mtl, "newmtl m%d_t%u\nmap_Kd textures/%03u_id%u.png\n", chunk_index, texture, texture,
                     bank_texture_id(model_id, texture));
        fprintf(mtl, "Kd %.4f %.4f %.4f\nKs %.4f %.4f %.4f\n\n", colours[12], colours[13], colours[14], colours[16],
                colours[17], colours[18]);
        cursor += CHUNK_HEADER_WORDS + (cursor[CHUNK_SIZE_WORD] >> 2);
        chunk_index++;
    }
    fclose(mtl);
}

static void dump_model(int model_id, const uint32_t *object) {
    char bank[BANK_NAME_LENGTH], folder[MAX_PATH], path[MAX_PATH], mtl_name[32];
    model_bank_name(model_id, bank, sizeof bank);
    snprintf(folder, sizeof folder, "%sdump", models_folder);
    CreateDirectoryA(models_folder, NULL);
    CreateDirectoryA(folder, NULL);
    snprintf(folder, sizeof folder, "%sdump\\%s", models_folder, bank);
    CreateDirectoryA(folder, NULL);
    snprintf(path, sizeof path, "%s\\%d.bin", folder, model_id);
    FILE *bin = fopen(path, "wb");
    if (bin) {
        fwrite(object, 1, object_size(object, MAX_OBJECT_BYTES), bin);
        fclose(bin);
    }
    snprintf(mtl_name, sizeof mtl_name, "%d.mtl", model_id);
    snprintf(path, sizeof path, "%s\\%d.obj", folder, model_id);
    write_object_obj(object, path, mtl_name);
    snprintf(path, sizeof path, "%s\\%s", folder, mtl_name);
    write_object_mtl(model_id, object, path);
}

static int read_text_file(const char *path, char **out, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, file) == (size_t)size) {
        text[size] = '\0';
        *out = text;
        if (out_size) *out_size = (size_t)size;
    } else {
        free(text);
        text = NULL;
    }
    fclose(file);
    return text != NULL;
}

static void *grow(void *array, int count, int *capacity, size_t element_size) {
    if (count < *capacity) return array;
    *capacity = *capacity ? *capacity * 2 : 256;
    return realloc(array, (size_t)*capacity * element_size);
}

static int obj_index(const char *field, int count) {
    int value = atoi(field);
    return value > 0 ? value - 1 : value < 0 ? count + value : -1;
}

static int find_material(obj_mesh *mesh, const char *name, int *capacity) {
    for (int i = 0; i < mesh->material_count; i++) {
        if (!strcmp(mesh->material_names[i], name)) return i;
    }
    mesh->material_names = grow(mesh->material_names, mesh->material_count, capacity, sizeof mesh->material_names[0]);
    lstrcpynA(mesh->material_names[mesh->material_count], name, 64);
    return mesh->material_count++;
}

static void free_obj(obj_mesh *mesh) {
    free(mesh->positions);
    free(mesh->uvs);
    free(mesh->normals);
    free(mesh->corners);
    free(mesh->corner_materials);
    free(mesh->material_names);
}

static int parse_obj(char *text, obj_mesh *mesh) {
    int position_capacity = 0, uv_capacity = 0, normal_capacity = 0, corner_capacity = 0, material_capacity = 0;
    int material = find_material(mesh, "", &material_capacity);
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        while (*line == ' ' || *line == '\t') line++;
        if (line[0] == 'v' && line[1] == ' ') {
            mesh->positions = grow(mesh->positions, mesh->position_count, &position_capacity, 3 * sizeof(float));
            float *p = mesh->positions + 3 * mesh->position_count++;
            p[0] = p[1] = p[2] = 0;
            sscanf(line + 2, "%f %f %f", &p[0], &p[1], &p[2]);
        } else if (line[0] == 'v' && line[1] == 't') {
            mesh->uvs = grow(mesh->uvs, mesh->uv_count, &uv_capacity, 2 * sizeof(float));
            float *t = mesh->uvs + 2 * mesh->uv_count++;
            t[0] = t[1] = 0;
            sscanf(line + 3, "%f %f", &t[0], &t[1]);
        } else if (line[0] == 'v' && line[1] == 'n') {
            mesh->normals = grow(mesh->normals, mesh->normal_count, &normal_capacity, 3 * sizeof(float));
            float *n = mesh->normals + 3 * mesh->normal_count++;
            n[0] = n[1] = n[2] = 0;
            sscanf(line + 3, "%f %f %f", &n[0], &n[1], &n[2]);
        } else if (!strncmp(line, "usemtl", 6)) {
            char name[64] = "";
            sscanf(line + 6, "%63s", name);
            material = find_material(mesh, name, &material_capacity);
        } else if (line[0] == 'f' && line[1] == ' ') {
            int polygon[64][3], corners = 0;
            char *cursor = line + 2;
            while (*cursor && corners < 64) {
                while (*cursor == ' ' || *cursor == '\t') cursor++;
                if (!*cursor) break;
                polygon[corners][0] = obj_index(cursor, mesh->position_count);
                polygon[corners][1] = polygon[corners][2] = -1;
                char *slash = cursor;
                while (*slash && *slash != '/' && *slash != ' ' && *slash != '\t') slash++;
                if (*slash == '/') {
                    if (slash[1] != '/') polygon[corners][1] = obj_index(slash + 1, mesh->uv_count);
                    char *second = slash + 1;
                    while (*second && *second != '/' && *second != ' ' && *second != '\t') second++;
                    if (*second == '/') polygon[corners][2] = obj_index(second + 1, mesh->normal_count);
                }
                while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
                corners++;
            }
            for (int i = 1; i + 1 < corners; i++) {
                int fan[3] = {0, i, i + 1};
                for (int k = 0; k < 3; k++) {
                    int old_capacity = corner_capacity;
                    mesh->corners = grow(mesh->corners, mesh->corner_count, &corner_capacity, 3 * sizeof(int));
                    if (corner_capacity != old_capacity) {
                        mesh->corner_materials = realloc(mesh->corner_materials, (size_t)corner_capacity * sizeof(int));
                    }
                    memcpy(mesh->corners + 3 * mesh->corner_count, polygon[fan[k]], 3 * sizeof(int));
                    mesh->corner_materials[mesh->corner_count++] = material;
                }
            }
        }
    }
    return mesh->corner_count >= 3 && mesh->position_count > 0;
}

typedef struct {
    const uint32_t *header;
    uint32_t strip_flags;
} chunk_template;

static int read_chunk_templates(const uint32_t *object, chunk_template *templates, int capacity) {
    const uint32_t *cursor = object + OBJECT_HEADER_SIZE / 4;
    int count = 0;
    while (*cursor >> 31 && count < capacity) {
        templates[count].header = cursor;
        const uint32_t *strip = cursor + CHUNK_HEADER_WORDS;
        templates[count].strip_flags = *strip && !(*strip >> 31) ? *strip : 0xF2;
        cursor += CHUNK_HEADER_WORDS + (cursor[CHUNK_SIZE_WORD] >> 2);
        count++;
    }
    return count;
}

static void material_choice(const char *name, int template_count, int *chunk, uint32_t *texture) {
    *chunk = 0;
    *texture = (uint32_t)-2;
    if (name[0] == 'm' && name[1] >= '0' && name[1] <= '9') *chunk = atoi(name + 1);
    if (*chunk >= template_count) *chunk = 0;
    const char *texture_part = strstr(name, "_t");
    if (texture_part && texture_part[2] >= '0' && texture_part[2] <= '9') *texture = (uint32_t)atoi(texture_part + 2);
    if (strstr(name, "_none")) *texture = NO_TEXTURE;
}

static void game_vertex(const obj_mesh *mesh, const int *corner, const float *face_normal, float *out) {
    const float *p = corner[0] >= 0 && corner[0] < mesh->position_count ? mesh->positions + 3 * corner[0] : NULL;
    const float *t = corner[1] >= 0 && corner[1] < mesh->uv_count ? mesh->uvs + 2 * corner[1] : NULL;
    const float *n = corner[2] >= 0 && corner[2] < mesh->normal_count ? mesh->normals + 3 * corner[2] : face_normal;
    out[0] = p ? p[0] : 0;
    out[1] = p ? p[1] : 0;
    out[2] = p ? -p[2] : 0;
    out[3] = n[0];
    out[4] = n[1];
    out[5] = -n[2];
    out[6] = t ? t[0] : 0;
    out[7] = t ? 1.0f - t[1] : 0;
    uint32_t bits;
    memcpy(&bits, &out[0], 4);
    bits |= 1;
    memcpy(&out[0], &bits, 4);
}

static void face_normal(const obj_mesh *mesh, const int *corners, float *out) {
    float v[3][3] = {{0}};
    for (int k = 0; k < 3; k++) {
        int i = corners[3 * k];
        if (i >= 0 && i < mesh->position_count) memcpy(v[k], mesh->positions + 3 * i, sizeof v[k]);
    }
    float a[3] = {v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]};
    float b[3] = {v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2]};
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
    float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    for (int k = 0; k < 3; k++) out[k] = length > 0 ? out[k] / length : (k == 1);
}

static void bounding_sphere(const float *vertices, int count, float *sphere) {
    float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < 3; k++) {
            float value = vertices[i * VERTEX_FLOATS + k];
            if (value < low[k]) low[k] = value;
            if (value > high[k]) high[k] = value;
        }
    }
    float radius = 0;
    for (int k = 0; k < 3; k++) sphere[k] = count ? (low[k] + high[k]) / 2 : 0;
    for (int i = 0; i < count; i++) {
        const float *v = vertices + i * VERTEX_FLOATS;
        float d = sqrtf((v[0] - sphere[0]) * (v[0] - sphere[0]) + (v[1] - sphere[1]) * (v[1] - sphere[1]) +
                        (v[2] - sphere[2]) * (v[2] - sphere[2]));
        if (d > radius) radius = d;
    }
    sphere[3] = radius;
}

static uint32_t *build_object_from_obj(const uint32_t *original, obj_mesh *mesh, size_t *out_size) {
    chunk_template templates[256];
    int template_count = read_chunk_templates(original, templates, 256);
    if (!template_count) return NULL;
    int triangle_count = mesh->corner_count / 3;
    size_t words = OBJECT_HEADER_SIZE / 4 + 8 + (size_t)mesh->material_count * CHUNK_HEADER_WORDS +
                   ((size_t)triangle_count / MAX_TRIANGLES_PER_LIST + mesh->material_count) * 2 +
                   (size_t)triangle_count * 3 * VERTEX_FLOATS;
    uint32_t *object = calloc(words, 4);
    float *all_vertices = malloc((size_t)triangle_count * 3 * VERTEX_FLOATS * sizeof(float));
    if (!object || !all_vertices) {
        free(object);
        free(all_vertices);
        return NULL;
    }
    memcpy(object, original, OBJECT_HEADER_SIZE);
    uint32_t *cursor = object + OBJECT_HEADER_SIZE / 4;
    int all_count = 0;
    for (int material = 0; material < mesh->material_count; material++) {
        int chunk;
        uint32_t texture;
        material_choice(mesh->material_names[material], template_count, &chunk, &texture);
        uint32_t *header = cursor;
        memcpy(header, templates[chunk].header, CHUNK_HEADER_SIZE);
        if (texture != (uint32_t)-2) header[CHUNK_TEXTURE_WORD] = texture;
        cursor += CHUNK_HEADER_WORDS;
        float *chunk_vertices = all_vertices + (size_t)all_count * VERTEX_FLOATS;
        int chunk_vertex_count = 0;
        uint32_t *list = NULL;
        for (int triangle = 0; triangle < triangle_count; triangle++) {
            if (mesh->corner_materials[triangle * 3] != material) continue;
            if (!list || list[1] == MAX_TRIANGLES_PER_LIST) {
                list = cursor;
                list[0] = (templates[chunk].strip_flags & ~STRIP_REFERENCES_FLAG) | STRIP_LIST_FLAG;
                list[1] = 0;
                cursor += 2;
            }
            const int *corners = mesh->corners + triangle * 9;
            float normal[3];
            face_normal(mesh, corners, normal);
            for (int k = 0; k < 3; k++) {
                game_vertex(mesh, corners + 3 * k, normal, (float *)cursor);
                memcpy(chunk_vertices + (size_t)chunk_vertex_count * VERTEX_FLOATS, cursor, VERTEX_FLOATS * sizeof(float));
                chunk_vertex_count++;
                cursor += VERTEX_FLOATS;
            }
            list[1]++;
        }
        if (!chunk_vertex_count) {
            cursor = header;
            continue;
        }
        bounding_sphere(chunk_vertices, chunk_vertex_count, (float *)(header + CHUNK_SPHERE_WORD));
        header[CHUNK_SIZE_WORD] = (uint32_t)((cursor - header - CHUNK_HEADER_WORDS) * 4);
        all_count += chunk_vertex_count;
    }
    *cursor++ = 0;
    bounding_sphere(all_vertices, all_count, (float *)(object + 2));
    free(all_vertices);
    *out_size = (size_t)(cursor - object) * 4;
    return object;
}

static int find_model_file(int model_id, const char *extension, char *path, size_t size) {
    char bank[BANK_NAME_LENGTH];
    model_bank_name(model_id, bank, sizeof bank);
    for (int i = 0; i < pack_count; i++) {
        snprintf(path, size, "%s%s\\%s\\%d.%s", models_folder, pack_names[i], bank, model_id, extension);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 1;
        snprintf(path, size, "%s%s\\%d.%s", models_folder, pack_names[i], model_id, extension);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}

static uint32_t *load_replacement(int model_id, const uint32_t *original, char *source, size_t source_size) {
    char *text = NULL;
    size_t size = 0;
    uint32_t *object = NULL;
    if (find_model_file(model_id, "bin", source, source_size) && read_text_file(source, &text, &size)) {
        if (size > OBJECT_HEADER_SIZE && object_size((const uint32_t *)text, size)) return (uint32_t *)text;
        log_line("models: %s is not a valid model object", source);
        free(text);
        return NULL;
    }
    if (!find_model_file(model_id, "obj", source, source_size) || !read_text_file(source, &text, NULL)) return NULL;
    obj_mesh mesh = {0};
    if (parse_obj(text, &mesh)) {
        object = build_object_from_obj(original, &mesh, &size);
        log_line("models: %d <- %s (%d triangles, %d materials)", model_id, source, mesh.corner_count / 3,
                 mesh.material_count);
    } else {
        log_line("models: %s has no triangles", source);
    }
    free_obj(&mesh);
    free(text);
    return object;
}

static void write_model_log(int model_id, const char *replacement) {
    if (!model_log || model_seen[model_id] & 2) return;
    model_seen[model_id] |= 2;
    char bank[BANK_NAME_LENGTH];
    model_bank_name(model_id, bank, sizeof bank);
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(model_log, "%02d:%02d:%02d.%03d\t%s\t%d\t%s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            bank, model_id, replacement ? replacement : "");
    fflush(model_log);
}

static uint32_t *replace_model(int model_id, uint32_t *original) {
    if (model_id < 0 || model_id > MAX_MODEL_ID || !original) return original;
    if (original == replacement_objects[model_id]) return original;
    if (dump_models && !(model_seen[model_id] & 1)) {
        model_seen[model_id] |= 1;
        dump_model(model_id, original);
    }
    char source[MAX_PATH] = "";
    uint32_t *replacement = pack_count ? load_replacement(model_id, original, source, sizeof source) : NULL;
    write_model_log(model_id, replacement ? source : NULL);
    if (!replacement) return original;
    free(replacement_objects[model_id]);
    replacement_objects[model_id] = replacement;
    MODEL_TABLE[model_id].object = replacement;
    return replacement;
}

static int __cdecl load_object_textures_hook(uint32_t *object, void *headers, void *data) {
    if (loading_model_id >= 0 && MODEL_TABLE[loading_model_id].object == object) {
        object = replace_model(loading_model_id, object);
    }
    return original_load_object_textures(object, headers, data);
}

static void replace_untextured_model(int model_id) {
    if (model_id < 0 || model_id > MAX_MODEL_ID || LOADING_MODEL_HAS_TEXTURES == 1) return;
    if (!(MODEL_TABLE[model_id].flags & MODEL_LOADED_FLAG)) return;
    replace_model(model_id, MODEL_TABLE[model_id].object);
}

static int __cdecl finish_single_model_hook(int *request) {
    loading_model_id = request[REQUEST_MODEL_ID];
    int result = original_finish_single_model(request);
    replace_untextured_model(loading_model_id);
    loading_model_id = -1;
    return result;
}

static int __cdecl finish_bank_model_hook(int *request) {
    loading_model_id = BANK_LOAD_NEXT_MODEL_ID ? *BANK_LOAD_NEXT_MODEL_ID : -1;
    int result = original_finish_bank_model(request);
    replace_untextured_model(loading_model_id);
    loading_model_id = -1;
    return result;
}

static int is_replaced(int model_id) {
    return model_id > 0 && model_id <= MAX_MODEL_ID && replacement_objects[model_id] &&
           MODEL_TABLE[model_id].object == replacement_objects[model_id];
}

int model_hooks_pack_folder(int index, char *out, size_t size) {
    if (index < 0 || index >= pack_count) return 0;
    snprintf(out, size, "%s%s\\", models_folder, pack_names[index]);
    return 1;
}

size_t model_hooks_object_size(const uint32_t *object) {
    return object ? object_size(object, MAX_OBJECT_BYTES) : 0;
}

uint32_t *model_hooks_replacement(int model_id) {
    return is_replaced(model_id) ? replacement_objects[model_id] : NULL;
}

static int __cdecl setup_belt_hook(int part, int vertices) {
    unsigned char *character = CURRENT_CHARACTER;
    if (belt_database && character) {
        short kind = *(short *)(character + CHARACTER_KIND_OFFSET);
        const int *entry = kind >= 0 && kind < MAX_CHARACTER_KIND ? belt_database[kind] : NULL;
        const int *const *parts = entry ? (const int *const *)entry[1] : NULL;
        const int *belt = parts ? parts[part] : NULL;
        if (belt && is_replaced(belt[0])) {
            unsigned char *slots = *(unsigned char **)(character + CHARACTER_BELTS_OFFSET);
            *(int *)(slots + 4 + part * BELT_SLOT_STRIDE) = 0;
            if (belts_skipped++ < 8) log_line("models: no waist belt for replaced model %d (kind %d)", belt[0], kind);
            return 0;
        }
    }
    return original_setup_belt(part, vertices);
}

static void read_pack_list(const char *ini_path) {
    char list[MAX_PACKS * PACK_NAME_LENGTH];
    GetPrivateProfileStringA("models", "packs", "", list, sizeof list, ini_path);
    for (char *name = strtok(list, ","); name && pack_count < MAX_PACKS; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (*name) lstrcpynA(pack_names[pack_count++], name, PACK_NAME_LENGTH);
    }
}

static MH_STATUS hook_address(void *target, void *hook, void **original) {
    MH_STATUS status = MH_CreateHook(target, hook, original);
    return status == MH_OK ? MH_EnableHook(target) : status;
}

void install_model_hooks(const char *game_folder, const char *ini_path) {
    snprintf(models_folder, sizeof models_folder, "%smodels\\", game_folder);
    read_pack_list(ini_path);
    dump_models = GetPrivateProfileIntA("models", "dump", 0, ini_path);
    int log_models = GetPrivateProfileIntA("debug", "log_models", 0, ini_path);
    if (!pack_count && !dump_models && !log_models) {
        log_line("models: no packs listed in [models] packs");
        return;
    }
    HMODULE table = GetModuleHandleA("Table.dll");
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    bank_of_model = table ? (const unsigned short *)GetProcAddress(table, "PbkTbl") : NULL;
    bank_file_names = table ? (const char *const *)GetProcAddress(table, "PolFileNameTbl") : NULL;
    bank_texture_headers = table ? (const uint32_t *const *)GetProcAddress(table, "TexHeaderTbl") : NULL;
    if (!hglna || !bank_of_model || !bank_file_names) {
        log_line("models: HglNa.dll or the Table.dll model tables not found");
        return;
    }
    if (log_models) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%stodmod_models.log", game_folder);
        model_log = fopen(path, "w");
        if (model_log) {
            fputs("time\tbank\tmodel_id\treplacement\n", model_log);
            fflush(model_log);
            log_line("models: logging every loaded model to %s", path);
        }
    }
    for (int i = 0; i < pack_count; i++) log_line("models: pack %d = %s", i + 1, pack_names[i]);
    belt_database = (const int *const *)GetProcAddress(table, "BeltVtxDb");
    if (pack_count && belt_database) {
        log_line("models: hook setup_belt %s",
                 MH_StatusToString(hook_address((void *)SETUP_BELT_VA, setup_belt_hook, (void **)&original_setup_belt)));
    }
    if (dump_models) log_line("models: dumping every loaded model to %sdump\\", models_folder);
    void *load_textures = (unsigned char *)hglna + (LOAD_OBJECT_TEXTURES_VA - HGLNA_PREFERRED_BASE);
    log_line("models: hooks load_object_textures %s, finish_single_model %s, finish_bank_model %s",
             MH_StatusToString(hook_address(load_textures, load_object_textures_hook, (void **)&original_load_object_textures)),
             MH_StatusToString(hook_address((void *)FINISH_SINGLE_MODEL_VA, finish_single_model_hook,
                                            (void **)&original_finish_single_model)),
             MH_StatusToString(hook_address((void *)FINISH_BANK_MODEL_VA, finish_bank_model_hook,
                                            (void **)&original_finish_bank_model)));
}
