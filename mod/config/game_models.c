#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "game_models.h"

#define HAB_HEADER_SIZE 0x20
#define HAB_ENTRY_SIZE 16
#define NO_BANK 0x102
#define MODEL_ID_LIMIT 0x24b3
#define OBJECT_HEADER_WORDS 6
#define CHUNK_HEADER_WORDS 20
#define CHUNK_TEXTURE_WORD 8
#define VERTEX_FLOATS 8
#define STRIP_LIST_FLAG 0x08
#define STRIP_REFERENCES_FLAG 0x20
#define NO_TEXTURE 0xFFFFFFFFu
#define MAX_BODY_DEPTH 32
#define MAX_KINDS 256
#define VQ_CODEBOOK_BYTES 2048
#define DATA_FORMAT_TWIDDLED_SQUARE 1
#define DATA_FORMAT_VQ 3
#define DATA_FORMAT_PALETTE4 5
#define DATA_FORMAT_RECTANGLE 9
#define DATA_FORMAT_TWIDDLED_RECTANGLE 13
#define MAX_LOADED_BANKS 16
#define MAX_MODEL_PACKS 8
#define PACK_NAME_LENGTH 64
#define PACK_CHARACTER_NAME_LENGTH 96
#define MAX_PACK_TEXTURES 32
#define GDIPLUS_PIXEL_FORMAT_32BPP_ARGB 0x26200A
#define GDIPLUS_LOCK_READ 1

typedef struct {
    uint16_t width, height;
    uint8_t pixel_format, data_format;
    uint16_t padding;
    uint32_t offset, id;
} texture_header;

typedef struct {
    FILE *file;
    uint32_t count;
    uint32_t data_base;
    uint32_t (*entries)[4];
    char *names;
} hab_archive;

typedef struct {
    int width, height;
    uint32_t *pixels;
} texture_image;

typedef struct {
    int bank;
    uint8_t *objects;
    size_t objects_size;
    int texture_count;
    texture_image *textures;
} loaded_bank;

typedef struct {
    float vertex[3][VERTEX_FLOATS];
    const texture_image *texture;
} triangle;

static HMODULE table_module;
static uintptr_t table_start, table_end;
static const uint8_t *const *bodies;
static const uint16_t *bank_of_model;
static const char *const *model_bank_files;
static const char *const *texture_bank_files;
static const int16_t *const *bank_model_ids;
static const uint16_t *bank_model_counts;
static const texture_header *const *bank_texture_headers;
static const int16_t *bank_texture_counts;
static hab_archive model_archive, texture_archive;
static int kind_total;
static char game_folder_path[MAX_PATH];
static char pack_names[MAX_MODEL_PACKS][PACK_NAME_LENGTH];
static int pack_count;
static char pack_character_names[MAX_KINDS][PACK_CHARACTER_NAME_LENGTH];

typedef struct {
    UINT32 version;
    void *debug_event_callback;
    BOOL suppress_background_thread;
    BOOL suppress_external_codecs;
} gdiplus_startup_input;

typedef struct {
    UINT width, height;
    INT stride, pixel_format;
    void *scan0;
    UINT_PTR reserved;
} gdiplus_bitmap_data;

typedef struct {
    INT x, y, width, height;
} gdiplus_rect;

int WINAPI GdiplusStartup(ULONG_PTR *token, const gdiplus_startup_input *input, void *output);
int WINAPI GdipCreateBitmapFromFile(const WCHAR *file, void **bitmap);
int WINAPI GdipGetImageWidth(void *image, UINT *width);
int WINAPI GdipGetImageHeight(void *image, UINT *height);
int WINAPI GdipBitmapLockBits(void *bitmap, const gdiplus_rect *rect, UINT flags, INT format, gdiplus_bitmap_data *data);
int WINAPI GdipBitmapUnlockBits(void *bitmap, gdiplus_bitmap_data *data);
int WINAPI GdipDisposeImage(void *image);

static ULONG_PTR gdiplus_token;
static int gdiplus_ready;

static int in_table(const void *pointer) {
    return (uintptr_t)pointer >= table_start && (uintptr_t)pointer < table_end;
}

static int hab_open(hab_archive *archive, const char *path) {
    archive->file = fopen(path, "rb");
    if (!archive->file) return 0;
    uint8_t header[HAB_HEADER_SIZE];
    if (fread(header, 1, sizeof header, archive->file) != sizeof header || memcmp(header, "HAB0", 4)) return 0;
    uint32_t names_size;
    memcpy(&archive->count, header + 0x14, 4);
    memcpy(&names_size, header + 0x18, 4);
    archive->count &= 0xFFFF;
    archive->entries = malloc((size_t)archive->count * HAB_ENTRY_SIZE);
    archive->names = malloc(names_size + 1);
    if (!archive->entries || !archive->names) return 0;
    if (fread(archive->entries, HAB_ENTRY_SIZE, archive->count, archive->file) != archive->count) return 0;
    if (fread(archive->names, 1, names_size, archive->file) != names_size) return 0;
    archive->names[names_size] = '\0';
    archive->data_base = HAB_HEADER_SIZE + archive->count * HAB_ENTRY_SIZE + names_size;
    return 1;
}

static void hab_close(hab_archive *archive) {
    if (archive->file) fclose(archive->file);
    free(archive->entries);
    free(archive->names);
    memset(archive, 0, sizeof *archive);
}

static uint8_t *hab_read(hab_archive *archive, const char *name, size_t *size) {
    for (uint32_t i = 0; archive->file && i < archive->count; i++) {
        if (_stricmp(archive->names + archive->entries[i][0], name)) continue;
        uint8_t *data = malloc(archive->entries[i][2] + 1);
        if (!data) return NULL;
        fseek(archive->file, (long)(archive->data_base + archive->entries[i][1]), SEEK_SET);
        *size = fread(data, 1, archive->entries[i][2], archive->file);
        return data;
    }
    return NULL;
}

typedef struct {
    const uint8_t *data;
    size_t size, position;
    unsigned control, bits_left;
    int overrun;
} prs_reader;

static unsigned prs_byte(prs_reader *reader) {
    if (reader->position >= reader->size) {
        reader->overrun = 1;
        return 0;
    }
    return reader->data[reader->position++];
}

static unsigned prs_bit(prs_reader *reader) {
    if (!reader->bits_left) {
        reader->control = prs_byte(reader);
        reader->bits_left = 8;
    }
    unsigned bit = reader->control & 1;
    reader->control >>= 1;
    reader->bits_left--;
    return bit;
}

static uint8_t *prs_decompress(const uint8_t *data, size_t size, size_t expected) {
    uint8_t *out = malloc(expected + 1);
    if (!out) return NULL;
    prs_reader reader = {data, size, 0, 0, 0, 0};
    size_t written = 0;
    while (!reader.overrun) {
        if (prs_bit(&reader)) {
            unsigned value = prs_byte(&reader);
            if (written >= expected) break;
            out[written++] = (uint8_t)value;
            continue;
        }
        size_t length, distance;
        if (prs_bit(&reader)) {
            unsigned packed = prs_byte(&reader);
            packed |= prs_byte(&reader) << 8;
            if (!packed) break;
            length = packed & 7;
            length = length ? length + 2 : (size_t)prs_byte(&reader) + 1;
            distance = 0x2000 - (packed >> 3);
        } else {
            unsigned high = prs_bit(&reader);
            unsigned low = prs_bit(&reader);
            length = (high << 1 | low) + 2;
            distance = 0x100 - prs_byte(&reader);
        }
        if (reader.overrun || distance > written || written + length > expected) break;
        for (size_t i = 0; i < length; i++, written++) out[written] = out[written - distance];
    }
    if (written != expected) {
        free(out);
        return NULL;
    }
    return out;
}

static uint8_t *bank_bytes(uint8_t *content, size_t size, size_t *out_size) {
    if (size >= 8) {
        uint32_t declared;
        memcpy(&declared, content, 4);
        if (declared > 0 && declared < 64u * 1024 * 1024) {
            uint8_t *raw = prs_decompress(content + 4, size - 4, declared);
            if (raw) {
                free(content);
                *out_size = declared;
                return raw;
            }
        }
    }
    *out_size = size;
    return content;
}

static unsigned spread_bits(unsigned value) {
    unsigned spread = 0;
    for (int bit = 0; bit < 12; bit++) spread |= ((value >> bit) & 1) << (2 * bit);
    return spread;
}

static unsigned twiddled_index(int column, int row, int width, int height) {
    int side = width < height ? width : height;
    unsigned inner = spread_bits((unsigned)(column % side)) << 1 | spread_bits((unsigned)(row % side));
    if (width > height) return inner + (unsigned)(column / side) * side * side;
    if (height > width) return inner + (unsigned)(row / side) * side * side;
    return inner;
}

static uint32_t pixel_argb(uint16_t value, int pixel_format) {
    unsigned r, g, b, a;
    if (pixel_format == 2) {
        a = ((value >> 12) & 15) * 17, r = ((value >> 8) & 15) * 17, g = ((value >> 4) & 15) * 17, b = (value & 15) * 17;
    } else if (pixel_format == 1) {
        a = 255, r = ((value >> 11) & 31) * 255 / 31, g = ((value >> 5) & 63) * 255 / 63, b = (value & 31) * 255 / 31;
    } else {
        a = (value >> 15) ? 255 : 0, r = ((value >> 10) & 31) * 255 / 31, g = ((value >> 5) & 31) * 255 / 31,
        b = (value & 31) * 255 / 31;
    }
    return a << 24 | r << 16 | g << 8 | b;
}

static void decode_texture(const uint8_t *data, size_t available, const texture_header *header, texture_image *image) {
    int width = header->width, height = header->height;
    image->width = width;
    image->height = height;
    image->pixels = malloc((size_t)width * height * 4);
    if (!image->pixels) return;
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            uint16_t value = 0x7FFF;
            size_t at = 0;
            int ok = 1;
            switch (header->data_format) {
            case DATA_FORMAT_RECTANGLE:
                at = ((size_t)row * width + column) * 2;
                break;
            case DATA_FORMAT_TWIDDLED_SQUARE:
            case DATA_FORMAT_TWIDDLED_RECTANGLE:
                at = (size_t)twiddled_index(column, row, width, height) * 2;
                break;
            case DATA_FORMAT_VQ: {
                size_t block = twiddled_index(column / 2, row / 2, width / 2, height / 2);
                if (VQ_CODEBOOK_BYTES + block >= available) {
                    ok = 0;
                    break;
                }
                unsigned code = data[VQ_CODEBOOK_BYTES + block];
                unsigned slot = (unsigned)(column & 1) * 2 + (unsigned)(row & 1);
                at = (size_t)code * 8 + slot * 2;
                break;
            }
            default:
                ok = 0;
            }
            if (ok && at + 1 < available) value = (uint16_t)(data[at] | data[at + 1] << 8);
            uint32_t argb = ok ? pixel_argb(value, header->pixel_format) : 0xFF808080u;
            image->pixels[(size_t)(height - 1 - row) * width + column] = argb;
        }
    }
}

static void free_bank(loaded_bank *bank) {
    free(bank->objects);
    for (int i = 0; i < bank->texture_count; i++) free(bank->textures[i].pixels);
    free(bank->textures);
    memset(bank, 0, sizeof *bank);
}

static int load_bank(int bank, loaded_bank *out) {
    memset(out, 0, sizeof *out);
    out->bank = bank;
    size_t size = 0;
    uint8_t *content = hab_read(&model_archive, model_bank_files[bank], &size);
    if (!content) return 0;
    out->objects = bank_bytes(content, size, &out->objects_size);
    const texture_header *headers = bank_texture_headers[bank];
    int count = bank_texture_counts[bank];
    if (!headers || count <= 0 || !texture_bank_files[bank]) return 1;
    content = hab_read(&texture_archive, texture_bank_files[bank], &size);
    if (!content) return 1;
    size_t raw_size;
    uint8_t *raw = bank_bytes(content, size, &raw_size);
    out->textures = calloc((size_t)count, sizeof *out->textures);
    if (out->textures) {
        out->texture_count = count;
        for (int i = 0; i < count; i++) {
            if (headers[i].offset < raw_size && headers[i].width && headers[i].height &&
                headers[i].width <= 1024 && headers[i].height <= 1024) {
                decode_texture(raw + headers[i].offset, raw_size - headers[i].offset, &headers[i], &out->textures[i]);
            }
        }
    }
    free(raw);
    return 1;
}

static const uint32_t *find_object(const loaded_bank *bank, int model_id, size_t *size) {
    const int16_t *ids = bank_model_ids[bank->bank];
    int count = bank_model_counts[bank->bank];
    if (!ids || !in_table(ids)) return NULL;
    for (int i = 0; i < count; i++) {
        if (ids[i] != model_id) continue;
        uint32_t start, object, next_start;
        if ((size_t)(i + 1) * 8 + 4 > bank->objects_size) return NULL;
        memcpy(&start, bank->objects + (size_t)i * 8, 4);
        memcpy(&object, bank->objects + (size_t)i * 8 + 4, 4);
        memcpy(&next_start, bank->objects + (size_t)(i + 1) * 8, 4);
        size_t end = (next_start & 0x7FFFFFFF) ? (next_start & 0x7FFFFFFF) : bank->objects_size;
        if (object >= bank->objects_size || end > bank->objects_size || end <= object) return NULL;
        *size = end - object;
        return (const uint32_t *)(bank->objects + object);
    }
    return NULL;
}

typedef struct {
    triangle *items;
    int count, capacity;
} triangle_list;

static void add_triangle(triangle_list *list, const float *a, const float *b, const float *c, const float *offset,
                         const texture_image *texture) {
    if (list->count == list->capacity) {
        int capacity = list->capacity ? list->capacity * 2 : 1024;
        triangle *grown = realloc(list->items, (size_t)capacity * sizeof *grown);
        if (!grown) return;
        list->items = grown;
        list->capacity = capacity;
    }
    triangle *t = &list->items[list->count++];
    const float *source[3] = {a, b, c};
    for (int k = 0; k < 3; k++) {
        memcpy(t->vertex[k], source[k], sizeof t->vertex[k]);
        for (int axis = 0; axis < 3; axis++) t->vertex[k][axis] += offset[axis];
    }
    t->texture = texture;
}

static void add_object_triangles(const uint32_t *object, size_t size, const loaded_bank *bank, const float *offset,
                                 triangle_list *list) {
    const uint32_t *end = object + size / 4;
    const uint32_t *cursor = object + OBJECT_HEADER_WORDS;
    const texture_image *texture = NULL;
    while (cursor < end && *cursor) {
        if (*cursor >> 31) {
            if (cursor + CHUNK_HEADER_WORDS > end) return;
            uint32_t index = cursor[CHUNK_TEXTURE_WORD];
            texture = index != NO_TEXTURE && (int)index < bank->texture_count && bank->textures[index].pixels
                          ? &bank->textures[index] : NULL;
            cursor += CHUNK_HEADER_WORDS;
            continue;
        }
        if (cursor + 2 > end) return;
        uint32_t flags = cursor[0], count = cursor[1];
        int is_list = (flags & STRIP_LIST_FLAG) != 0;
        if (is_list) count *= 3;
        cursor += 2;
        const float *previous[2] = {NULL, NULL};
        const float *corners[3] = {NULL, NULL, NULL};
        for (uint32_t i = 0; i < count && cursor < end; i++) {
            const float *vertex;
            if ((flags & STRIP_REFERENCES_FLAG) && !(cursor[0] & 1)) {
                vertex = (const float *)((const char *)(cursor + 2) + (int32_t)cursor[1]);
                cursor += 2;
            } else {
                vertex = (const float *)cursor;
                cursor += VERTEX_FLOATS;
            }
            if ((const uint32_t *)vertex < object || (const uint32_t *)vertex + VERTEX_FLOATS > end) return;
            if (is_list) {
                corners[i % 3] = vertex;
                if (i % 3 == 2) add_triangle(list, corners[0], corners[1], corners[2], offset, texture);
            } else {
                if (previous[0] && previous[1]) {
                    if (i & 1) add_triangle(list, previous[1], previous[0], vertex, offset, texture);
                    else add_triangle(list, previous[0], previous[1], vertex, offset, texture);
                }
                previous[0] = previous[1];
                previous[1] = vertex;
            }
        }
    }
}

typedef struct {
    char path[MAX_PATH];
    texture_image image;
} pack_texture;

typedef struct {
    loaded_bank banks[MAX_LOADED_BANKS];
    int bank_count;
    triangle_list triangles;
    pack_texture pack_textures[MAX_PACK_TEXTURES];
    int pack_texture_count;
} character_scene;

static int load_png(const char *path, texture_image *image) {
    memset(image, 0, sizeof *image);
    if (!gdiplus_ready) {
        gdiplus_startup_input input = {1, NULL, FALSE, FALSE};
        gdiplus_ready = GdiplusStartup(&gdiplus_token, &input, NULL) == 0 ? 1 : -1;
    }
    if (gdiplus_ready != 1) return 0;
    WCHAR wide[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH);
    void *bitmap = NULL;
    if (GdipCreateBitmapFromFile(wide, &bitmap) != 0 || !bitmap) return 0;
    UINT width = 0, height = 0;
    GdipGetImageWidth(bitmap, &width);
    GdipGetImageHeight(bitmap, &height);
    gdiplus_rect rect = {0, 0, (INT)width, (INT)height};
    gdiplus_bitmap_data data;
    if (width && height && width <= 4096 && height <= 4096 &&
        GdipBitmapLockBits(bitmap, &rect, GDIPLUS_LOCK_READ, GDIPLUS_PIXEL_FORMAT_32BPP_ARGB, &data) == 0) {
        image->pixels = malloc((size_t)width * height * 4);
        if (image->pixels) {
            image->width = (int)width;
            image->height = (int)height;
            for (UINT row = 0; row < height; row++) {
                memcpy(image->pixels + (size_t)row * width, (const uint8_t *)data.scan0 + (size_t)data.stride * row,
                       (size_t)width * 4);
            }
        }
        GdipBitmapUnlockBits(bitmap, &data);
    }
    GdipDisposeImage(bitmap);
    return image->pixels != NULL;
}

static void bank_folder_name(const char *file_name, char *out, size_t size) {
    snprintf(out, size, "%s", file_name ? file_name : "unknown");
    char *dot = strrchr(out, '.');
    if (dot && !_stricmp(dot, ".bin")) *dot = '\0';
}

static int find_pack_model(int model_id, int bank, char *path, size_t size) {
    char bank_name[MAX_PATH];
    bank_folder_name(model_bank_files[bank], bank_name, sizeof bank_name);
    for (int i = 0; i < pack_count; i++) {
        snprintf(path, size, "%smodels\\%s\\%s\\%d.obj", game_folder_path, pack_names[i], bank_name, model_id);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 1;
        snprintf(path, size, "%smodels\\%s\\%d.obj", game_folder_path, pack_names[i], model_id);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}

static const texture_image *pack_texture_for(character_scene *scene, int bank, int slot) {
    char bank_name[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH] = "";
    bank_folder_name(texture_bank_files[bank], bank_name, sizeof bank_name);
    for (int i = 0; i < pack_count && !path[0]; i++) {
        snprintf(pattern, sizeof pattern, "%stextures\\%s\\%s\\%03d*.png", game_folder_path, pack_names[i], bank_name, slot);
        WIN32_FIND_DATAA found;
        HANDLE search = FindFirstFileA(pattern, &found);
        if (search == INVALID_HANDLE_VALUE) continue;
        FindClose(search);
        snprintf(path, sizeof path, "%stextures\\%s\\%s\\%s", game_folder_path, pack_names[i], bank_name, found.cFileName);
    }
    if (!path[0]) return NULL;
    for (int i = 0; i < scene->pack_texture_count; i++) {
        if (!strcmp(scene->pack_textures[i].path, path)) return scene->pack_textures[i].image.pixels ? &scene->pack_textures[i].image : NULL;
    }
    if (scene->pack_texture_count == MAX_PACK_TEXTURES) return NULL;
    pack_texture *loaded = &scene->pack_textures[scene->pack_texture_count++];
    snprintf(loaded->path, sizeof loaded->path, "%s", path);
    load_png(path, &loaded->image);
    return loaded->image.pixels ? &loaded->image : NULL;
}

static const texture_image *material_texture(character_scene *scene, const loaded_bank *bank, const char *material) {
    const char *slot_text = strstr(material, "_t");
    if (!slot_text || slot_text[2] < '0' || slot_text[2] > '9') return NULL;
    int slot = atoi(slot_text + 2);
    const texture_image *texture = pack_texture_for(scene, bank->bank, slot);
    if (texture) return texture;
    return slot < bank->texture_count && bank->textures[slot].pixels ? &bank->textures[slot] : NULL;
}

static void add_obj_triangles(character_scene *scene, const char *path, const loaded_bank *bank, const float *offset) {
    FILE *file = fopen(path, "r");
    if (!file) return;
    float *positions = NULL, *uvs = NULL;
    int position_count = 0, uv_count = 0, position_capacity = 0, uv_capacity = 0;
    const texture_image *texture = NULL;
    char line[512];
    while (fgets(line, sizeof line, file)) {
        if (line[0] == 'v' && line[1] == ' ') {
            if (position_count == position_capacity) {
                position_capacity = position_capacity ? position_capacity * 2 : 256;
                positions = realloc(positions, (size_t)position_capacity * 3 * sizeof *positions);
            }
            float *p = positions + (size_t)position_count++ * 3;
            if (sscanf(line + 2, "%f %f %f", &p[0], &p[1], &p[2]) != 3) p[0] = p[1] = p[2] = 0;
            p[2] = -p[2];
        } else if (line[0] == 'v' && line[1] == 't') {
            if (uv_count == uv_capacity) {
                uv_capacity = uv_capacity ? uv_capacity * 2 : 256;
                uvs = realloc(uvs, (size_t)uv_capacity * 2 * sizeof *uvs);
            }
            float *t = uvs + (size_t)uv_count++ * 2;
            if (sscanf(line + 3, "%f %f", &t[0], &t[1]) != 2) t[0] = t[1] = 0;
        } else if (!strncmp(line, "usemtl ", 7)) {
            line[strcspn(line, "\r\n")] = '\0';
            texture = material_texture(scene, bank, line + 7);
        } else if (line[0] == 'f' && line[1] == ' ' && positions) {
            int corner_position[3], corner_uv[3], corners = 0;
            char *cursor = line + 2;
            while (corners < 3 && *cursor) {
                int p = 0, t = 0;
                if (sscanf(cursor, "%d/%d", &p, &t) < 1) break;
                corner_position[corners] = p - 1;
                corner_uv[corners] = t - 1;
                corners++;
                while (*cursor && *cursor != ' ') cursor++;
                while (*cursor == ' ') cursor++;
            }
            if (corners < 3) continue;
            float vertices[3][VERTEX_FLOATS] = {{0}};
            for (int k = 0; k < 3; k++) {
                if (corner_position[k] < 0 || corner_position[k] >= position_count) goto next_line;
                memcpy(vertices[k], positions + (size_t)corner_position[k] * 3, 3 * sizeof(float));
                if (uvs && corner_uv[k] >= 0 && corner_uv[k] < uv_count) {
                    vertices[k][6] = uvs[(size_t)corner_uv[k] * 2];
                    vertices[k][7] = uvs[(size_t)corner_uv[k] * 2 + 1];
                }
            }
            float a[3], b[3], normal[3];
            for (int axis = 0; axis < 3; axis++) {
                a[axis] = vertices[1][axis] - vertices[0][axis];
                b[axis] = vertices[2][axis] - vertices[0][axis];
            }
            normal[0] = -(a[1] * b[2] - a[2] * b[1]);
            normal[1] = -(a[2] * b[0] - a[0] * b[2]);
            normal[2] = -(a[0] * b[1] - a[1] * b[0]);
            float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            if (length < 1e-9f) continue;
            for (int k = 0; k < 3; k++) {
                for (int axis = 0; axis < 3; axis++) vertices[k][3 + axis] = normal[axis] / length;
            }
            add_triangle(&scene->triangles, vertices[0], vertices[1], vertices[2], offset, texture);
        }
    next_line:;
    }
    fclose(file);
    free(positions);
    free(uvs);
}

static loaded_bank *scene_bank(character_scene *scene, int bank) {
    for (int i = 0; i < scene->bank_count; i++) {
        if (scene->banks[i].bank == bank) return &scene->banks[i];
    }
    if (scene->bank_count == MAX_LOADED_BANKS) return NULL;
    loaded_bank *loaded = &scene->banks[scene->bank_count];
    if (!load_bank(bank, loaded)) return NULL;
    scene->bank_count++;
    return loaded;
}

static void add_body_node(character_scene *scene, const uint8_t *node, const float *origin, int depth) {
    if (!in_table(node) || depth > MAX_BODY_DEPTH) return;
    uint32_t model_id;
    float position[3];
    int16_t children;
    memcpy(&model_id, node, 4);
    memcpy(position, node + 4, sizeof position);
    memcpy(&children, node + 0x16, 2);
    for (int axis = 0; axis < 3; axis++) position[axis] += origin[axis];
    if (model_id && model_id < MODEL_ID_LIMIT && bank_of_model[model_id] != NO_BANK) {
        loaded_bank *bank = scene_bank(scene, bank_of_model[model_id]);
        char pack_model[MAX_PATH];
        if (bank && find_pack_model((int)model_id, bank->bank, pack_model, sizeof pack_model)) {
            add_obj_triangles(scene, pack_model, bank, position);
        } else {
            size_t size = 0;
            const uint32_t *object = bank ? find_object(bank, (int)model_id, &size) : NULL;
            if (object) add_object_triangles(object, size, bank, position, &scene->triangles);
        }
    }
    for (int i = 0; i < children; i++) {
        const uint8_t *child;
        memcpy(&child, node + 0x18 + 4 * i, 4);
        add_body_node(scene, child, position, depth + 1);
    }
}

static void free_scene(character_scene *scene) {
    for (int i = 0; i < scene->bank_count; i++) free_bank(&scene->banks[i]);
    for (int i = 0; i < scene->pack_texture_count; i++) free(scene->pack_textures[i].image.pixels);
    free(scene->triangles.items);
    memset(scene, 0, sizeof *scene);
}

static uint32_t sample(const texture_image *texture, float u, float v) {
    if (!texture) return 0xFFC8C8C8u;
    u -= floorf(u);
    v -= floorf(v);
    int x = (int)(u * texture->width), y = (int)(v * texture->height);
    if (x >= texture->width) x = texture->width - 1;
    if (y >= texture->height) y = texture->height - 1;
    return texture->pixels[(size_t)y * texture->width + x];
}

static void rasterize(const character_scene *scene, uint32_t *pixels, float *depth, int width, int height, float yaw) {
    float cosine = cosf(yaw), sine = sinf(yaw);
    float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < scene->triangles.count; i++) {
        for (int k = 0; k < 3; k++) {
            const float *v = scene->triangles.items[i].vertex[k];
            float p[3] = {v[0] * cosine + v[2] * sine, v[1], -v[0] * sine + v[2] * cosine};
            for (int axis = 0; axis < 3; axis++) {
                if (p[axis] < low[axis]) low[axis] = p[axis];
                if (p[axis] > high[axis]) high[axis] = p[axis];
            }
        }
    }
    float span_x = high[0] - low[0], span_y = high[1] - low[1];
    float scale = 0.92f * fminf(width / fmaxf(span_x, 1e-3f), height / fmaxf(span_y, 1e-3f));
    float center_x = (low[0] + high[0]) / 2, center_y = (low[1] + high[1]) / 2;
    const float light[3] = {0.35f, 0.55f, 0.76f};
    for (int i = 0; i < scene->triangles.count; i++) {
        const triangle *t = &scene->triangles.items[i];
        float sx[3], sy[3], sz[3], shade[3];
        for (int k = 0; k < 3; k++) {
            const float *v = t->vertex[k];
            float x = v[0] * cosine + v[2] * sine, z = -v[0] * sine + v[2] * cosine;
            float nx = v[3] * cosine + v[5] * sine, nz = -v[3] * sine + v[5] * cosine;
            sx[k] = (x - center_x) * scale + width / 2.0f;
            sy[k] = height / 2.0f - (v[1] - center_y) * scale;
            sz[k] = z;
            float lambert = nx * light[0] + v[4] * light[1] + nz * light[2];
            shade[k] = 0.45f + 0.55f * fmaxf(lambert, 0.0f);
        }
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (fabsf(area) < 1e-6f) continue;
        int x0 = (int)fmaxf(floorf(fminf(sx[0], fminf(sx[1], sx[2]))), 0);
        int x1 = (int)fminf(ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2]))), (float)width - 1);
        int y0 = (int)fmaxf(floorf(fminf(sy[0], fminf(sy[1], sy[2]))), 0);
        int y1 = (int)fminf(ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2]))), (float)height - 1);
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                size_t at = (size_t)y * width + x;
                if (z <= depth[at]) continue;
                float u = w0 * t->vertex[0][6] + w1 * t->vertex[1][6] + w2 * t->vertex[2][6];
                float v = w0 * t->vertex[0][7] + w1 * t->vertex[1][7] + w2 * t->vertex[2][7];
                uint32_t texel = sample(t->texture, u, v);
                if ((texel >> 24) < 128) continue;
                float light_level = w0 * shade[0] + w1 * shade[1] + w2 * shade[2];
                unsigned r = (unsigned)fminf(((texel >> 16) & 255) * light_level, 255);
                unsigned g = (unsigned)fminf(((texel >> 8) & 255) * light_level, 255);
                unsigned b = (unsigned)fminf((texel & 255) * light_level, 255);
                pixels[at] = 0xFF000000u | r << 16 | g << 8 | b;
                depth[at] = z;
            }
        }
    }
}

int game_models_open(const char *game_folder) {
    snprintf(game_folder_path, sizeof game_folder_path, "%s", game_folder);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sTable.dll", game_folder);
    table_module = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!table_module) return 0;
    const IMAGE_NT_HEADERS *headers =
        (const IMAGE_NT_HEADERS *)((const uint8_t *)table_module + ((const IMAGE_DOS_HEADER *)table_module)->e_lfanew);
    table_start = (uintptr_t)table_module;
    table_end = table_start + headers->OptionalHeader.SizeOfImage;
    bodies = (const uint8_t *const *)GetProcAddress(table_module, "BodyInitTbl");
    bank_of_model = (const uint16_t *)GetProcAddress(table_module, "PbkTbl");
    model_bank_files = (const char *const *)GetProcAddress(table_module, "PolFileNameTbl");
    texture_bank_files = (const char *const *)GetProcAddress(table_module, "TexFileNameTbl");
    bank_model_ids = (const int16_t *const *)GetProcAddress(table_module, "PatNoTbl");
    bank_model_counts = (const uint16_t *)GetProcAddress(table_module, "BankPatMax");
    bank_texture_headers = (const texture_header *const *)GetProcAddress(table_module, "TexHeaderTbl");
    bank_texture_counts = (const int16_t *)GetProcAddress(table_module, "BankTexMax");
    if (!bodies || !bank_of_model || !model_bank_files || !texture_bank_files || !bank_model_ids ||
        !bank_model_counts || !bank_texture_headers || !bank_texture_counts) {
        game_models_close();
        return 0;
    }
    for (kind_total = 0; kind_total < MAX_KINDS && in_table(bodies[kind_total]); kind_total++) {
    }
    snprintf(path, sizeof path, "%spol.HAB", game_folder);
    int ok = hab_open(&model_archive, path);
    snprintf(path, sizeof path, "%stex.HAB", game_folder);
    ok = hab_open(&texture_archive, path) && ok;
    if (!ok) game_models_close();
    return ok;
}

void game_models_close(void) {
    hab_close(&model_archive);
    hab_close(&texture_archive);
    if (table_module) FreeLibrary(table_module);
    table_module = NULL;
    kind_total = 0;
}

static void read_pack_character_names(const char *pack) {
    char path[MAX_PATH], line[256];
    snprintf(path, sizeof path, "%smodels\\%s\\characters.tsv", game_folder_path, pack);
    FILE *file = fopen(path, "r");
    if (!file) return;
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#') continue;
        char *tab = strchr(line, '\t');
        int kind = atoi(line);
        if (!tab || kind < 0 || kind >= MAX_KINDS || pack_character_names[kind][0]) continue;
        tab[strcspn(tab, "\r\n")] = '\0';
        snprintf(pack_character_names[kind], sizeof pack_character_names[kind], "%s", tab + 1);
    }
    fclose(file);
}

void game_models_set_packs(const char *pack_list) {
    char list[MAX_MODEL_PACKS * PACK_NAME_LENGTH];
    snprintf(list, sizeof list, "%s", pack_list ? pack_list : "");
    pack_count = 0;
    memset(pack_character_names, 0, sizeof pack_character_names);
    for (char *name = strtok(list, ","); name && pack_count < MAX_MODEL_PACKS; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (!*name) continue;
        snprintf(pack_names[pack_count++], PACK_NAME_LENGTH, "%s", name);
        read_pack_character_names(name);
    }
}

const char *game_models_pack_name(int kind) {
    return kind >= 0 && kind < MAX_KINDS && pack_character_names[kind][0] ? pack_character_names[kind] : NULL;
}

int game_models_kind_count(void) {
    return kind_total;
}

int game_models_render_character(int kind, uint32_t *pixels, int width, int height, float yaw) {
    for (int y = 0; y < height; y++) {
        unsigned level = 52 + 34 * (unsigned)y / (unsigned)height;
        for (int x = 0; x < width; x++) pixels[(size_t)y * width + x] = 0xFF000000u | level << 16 | level << 8 | (level + 6);
    }
    if (kind < 0 || kind >= kind_total) return 0;
    character_scene scene = {0};
    const float origin[3] = {0, 0, 0};
    add_body_node(&scene, bodies[kind], origin, 0);
    float *depth = malloc((size_t)width * height * sizeof *depth);
    int drawn = scene.triangles.count;
    if (depth && drawn) {
        for (size_t i = 0; i < (size_t)width * height; i++) depth[i] = -1e30f;
        rasterize(&scene, pixels, depth, width, height, yaw);
    }
    free(depth);
    free_scene(&scene);
    return drawn;
}
