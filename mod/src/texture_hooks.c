#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "texture_hooks.h"
#include "blood.h"
#include "paintball.h"
#include "log.h"
#include "MinHook.h"

#define HGLNA_PREFERRED_BASE 0x10000000
#define CREATE_TEXTURE_VA 0x10006ab0
#define MAX_PACKS 16
#define PACK_NAME_LENGTH 64
#define MAX_BANKS 1024
#define MAX_PACK_BANKS 1024
#define BANK_NAME_LENGTH 64
#define DATA_FORMAT_RECTANGLE 9
#define PIXEL_FORMAT_ARGB1555 0
#define PIXEL_FORMAT_RGB565 1
#define PIXEL_FORMAT_ARGB4444 2
#define GDIPLUS_PIXEL_FORMAT_32BPP_ARGB 0x26200A
#define GDIPLUS_LOCK_READ 1
#define SEEN_SLOTS 8192
#define MAX_TEXTURE_SIDE 4096
#define MAX_SCALE 8

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t pixel_format;
    uint8_t data_format;
    uint16_t padding;
    uint32_t offset;
    uint32_t id;
} texture_header;

typedef void *(__cdecl *create_texture_fn)(const texture_header *header, const void *pixels);

typedef struct {
    const texture_header *first;
    const texture_header *end;
    int bank;
} bank_range;

typedef struct {
    UINT32 version;
    void *debug_event_callback;
    BOOL suppress_background_thread;
    BOOL suppress_external_codecs;
} gdiplus_startup_input;

typedef struct {
    UINT width;
    UINT height;
    INT stride;
    INT pixel_format;
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

static create_texture_fn original_create_texture;
static char texture_folder[MAX_PATH];
static char pack_names[MAX_PACKS][PACK_NAME_LENGTH];
static int pack_count;
static char (*pack_banks[MAX_PACKS])[BANK_NAME_LENGTH];
static int pack_bank_count[MAX_PACKS];
static const char *const *bank_file_names;
static bank_range bank_ranges[MAX_BANKS];
static int bank_range_count;
static int log_textures;
static FILE *texture_log;
static uint32_t seen_textures[SEEN_SLOTS];
static ULONG_PTR gdiplus_token;
static int gdiplus_ready;

static int compare_bank_ranges(const void *a, const void *b) {
    const bank_range *left = a, *right = b;
    return left->first < right->first ? -1 : left->first > right->first;
}

static void read_bank_table(void) {
    HMODULE table = GetModuleHandleA("Table.dll");
    const texture_header *const *headers = table ? (const texture_header *const *)GetProcAddress(table, "TexHeaderTbl") : NULL;
    const short *texture_counts = table ? (const short *)GetProcAddress(table, "BankTexMax") : NULL;
    bank_file_names = table ? (const char *const *)GetProcAddress(table, "TexFileNameTbl") : NULL;
    if (!headers || !texture_counts || !bank_file_names) {
        log_line("textures: Table.dll texture table not found");
        return;
    }
    for (int bank = 1; bank < MAX_BANKS && bank_file_names[bank] && headers[bank]; bank++) {
        if (texture_counts[bank] <= 0) continue;
        bank_ranges[bank_range_count].first = headers[bank];
        bank_ranges[bank_range_count].end = headers[bank] + texture_counts[bank];
        bank_ranges[bank_range_count].bank = bank;
        bank_range_count++;
    }
    qsort(bank_ranges, bank_range_count, sizeof bank_ranges[0], compare_bank_ranges);
}

static int find_texture(const texture_header *header, int *bank, int *index) {
    int low = 0, high = bank_range_count - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        if (header < bank_ranges[middle].first) {
            high = middle - 1;
        } else if (header >= bank_ranges[middle].end) {
            low = middle + 1;
        } else {
            *bank = bank_ranges[middle].bank;
            *index = (int)(header - bank_ranges[middle].first);
            return 1;
        }
    }
    return 0;
}

static void bank_folder_name(int bank, char *out, size_t size) {
    lstrcpynA(out, bank_file_names[bank], (int)size);
    char *extension = strrchr(out, '.');
    if (extension && _stricmp(extension, ".bin") == 0) *extension = '\0';
}

static void add_packs(const char *section, int only_with_textures, const char *ini_path) {
    char list[MAX_PACKS * PACK_NAME_LENGTH];
    GetPrivateProfileStringA(section, "packs", "", list, sizeof list, ini_path);
    for (char *name = strtok(list, ","); name && pack_count < MAX_PACKS; name = strtok(NULL, ",")) {
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = '\0';
        if (!*name) continue;
        int listed = 0;
        for (int i = 0; i < pack_count; i++) listed |= !_stricmp(pack_names[i], name);
        char folder[MAX_PATH];
        snprintf(folder, sizeof folder, "%s%s", texture_folder, name);
        if (listed || (only_with_textures && GetFileAttributesA(folder) == INVALID_FILE_ATTRIBUTES)) continue;
        lstrcpynA(pack_names[pack_count++], name, PACK_NAME_LENGTH);
    }
}

static void read_pack_list(const char *ini_path) {
    add_packs("models", 1, ini_path);
    add_packs("textures", 0, ini_path);
}

static void list_pack_banks(int pack) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s%s\\*", texture_folder, pack_names[pack]);
    pack_banks[pack] = calloc(MAX_PACK_BANKS, BANK_NAME_LENGTH);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found.cFileName[0] != '.' &&
            pack_bank_count[pack] < MAX_PACK_BANKS) {
            lstrcpynA(pack_banks[pack][pack_bank_count[pack]++], found.cFileName, BANK_NAME_LENGTH);
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static int pack_has_bank(int pack, const char *bank_name) {
    for (int i = 0; i < pack_bank_count[pack]; i++) {
        if (_stricmp(pack_banks[pack][i], bank_name) == 0) return 1;
    }
    return 0;
}

typedef struct {
    uint32_t id;
    int pack;
    char *path;
} id_override;

static id_override *id_overrides;
static int id_override_count, id_override_capacity;

static int texture_id_in_name(const char *name, uint32_t *id) {
    for (const char *at = name; (at = strstr(at, "id")) != NULL; at += 2) {
        int starts_word = at == name || at[-1] == '_' || at[-1] == '-' || at[-1] == ' ';
        if (!starts_word || at[2] < '0' || at[2] > '9') continue;
        char *end;
        unsigned long value = strtoul(at + 2, &end, 10);
        if (*end == '.' || *end == '_' || *end == '-' || *end == ' ') {
            *id = (uint32_t)value;
            return 1;
        }
    }
    return 0;
}

static void index_id_overrides(int pack, const char *folder, int depth) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s\\*", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (found.cFileName[0] == '.') continue;
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\%s", folder, found.cFileName);
        uint32_t id;
        const char *extension = strrchr(found.cFileName, '.');
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 3) index_id_overrides(pack, path, depth + 1);
        } else if (extension && !_stricmp(extension, ".png") && texture_id_in_name(found.cFileName, &id)) {
            if (id_override_count == id_override_capacity) {
                id_override_capacity = id_override_capacity ? id_override_capacity * 2 : 256;
                id_overrides = realloc(id_overrides, (size_t)id_override_capacity * sizeof *id_overrides);
            }
            id_overrides[id_override_count++] = (id_override){id, pack, _strdup(path)};
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static int compare_id_overrides(const void *a, const void *b) {
    const id_override *left = a, *right = b;
    if (left->id != right->id) return left->id < right->id ? -1 : 1;
    if (left->pack != right->pack) return left->pack - right->pack;
    return strcmp(left->path, right->path);
}

static const id_override *find_id_override(uint32_t id, int pack) {
    int low = 0, high = id_override_count - 1, first = -1;
    while (low <= high) {
        int middle = (low + high) / 2;
        if (id_overrides[middle].id < id) low = middle + 1;
        else {
            if (id_overrides[middle].id == id) first = middle;
            high = middle - 1;
        }
    }
    for (int i = first; i >= 0 && i < id_override_count && id_overrides[i].id == id; i++) {
        if (id_overrides[i].pack == pack) return &id_overrides[i];
    }
    return NULL;
}

static int find_replacement(const char *bank_name, int index, uint32_t id, char *path, size_t size) {
    for (int pack = 0; pack < pack_count; pack++) {
        if (bank_name && pack_has_bank(pack, bank_name)) {
            char pattern[MAX_PATH];
            snprintf(pattern, sizeof pattern, "%s%s\\%s\\%03d*.png", texture_folder, pack_names[pack], bank_name, index);
            WIN32_FIND_DATAA found;
            HANDLE search = FindFirstFileA(pattern, &found);
            if (search != INVALID_HANDLE_VALUE) {
                FindClose(search);
                snprintf(path, size, "%s%s\\%s\\%s", texture_folder, pack_names[pack], bank_name, found.cFileName);
                return 1;
            }
        }
        const id_override *by_id = find_id_override(id, pack);
        if (by_id) {
            lstrcpynA(path, by_id->path, (int)size);
            return 1;
        }
    }
    return 0;
}

static uint16_t encode_pixel(const uint8_t *bgra, int pixel_format) {
    unsigned blue = bgra[0], green = bgra[1], red = bgra[2], alpha = bgra[3];
    switch (pixel_format) {
    case PIXEL_FORMAT_RGB565:
        return (uint16_t)(((red * 31 + 127) / 255) << 11 | ((green * 63 + 127) / 255) << 5 | (blue * 31 + 127) / 255);
    case PIXEL_FORMAT_ARGB4444:
        return (uint16_t)(((alpha * 15 + 127) / 255) << 12 | ((red * 15 + 127) / 255) << 8 |
                          ((green * 15 + 127) / 255) << 4 | (blue * 15 + 127) / 255);
    default:
        return (uint16_t)((alpha >= 128) << 15 | ((red * 31 + 127) / 255) << 10 | ((green * 31 + 127) / 255) << 5 |
                          (blue * 31 + 127) / 255);
    }
}

static int is_power_of_two(UINT value) {
    return value && !(value & (value - 1));
}

static int texture_size_allowed(UINT width, UINT height, const texture_header *header) {
    if (width > MAX_TEXTURE_SIDE || height > MAX_TEXTURE_SIDE) return 0;
    for (int scale = 1; scale <= MAX_SCALE; scale *= 2) {
        if (width == (UINT)header->width * scale && height == (UINT)header->height * scale) return 1;
    }
    return is_power_of_two(width) && is_power_of_two(height);
}

static uint16_t *load_png_pixels(const char *path, const texture_header *header, UINT *out_width, UINT *out_height) {
    if (!gdiplus_ready) {
        gdiplus_startup_input input = {1, NULL, FALSE, FALSE};
        gdiplus_ready = GdiplusStartup(&gdiplus_token, &input, NULL) == 0 ? 1 : -1;
    }
    if (gdiplus_ready != 1) return NULL;
    WCHAR wide_path[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path, -1, wide_path, MAX_PATH);
    void *bitmap = NULL;
    if (GdipCreateBitmapFromFile(wide_path, &bitmap) != 0 || !bitmap) {
        log_line("textures: cannot read %s", path);
        return NULL;
    }
    UINT width = 0, height = 0;
    GdipGetImageWidth(bitmap, &width);
    GdipGetImageHeight(bitmap, &height);
    uint16_t *pixels = NULL;
    if (!texture_size_allowed(width, height, header)) {
        log_line("textures: %s is %ux%u; the game texture is %ux%u (use 1, 2, 4 or 8 times that, or power-of-two "
                 "sides, up to %d); skipped",
                 path, width, height, header->width, header->height, MAX_TEXTURE_SIDE);
    } else {
        *out_width = width;
        *out_height = height;
        gdiplus_rect rect = {0, 0, (INT)width, (INT)height};
        gdiplus_bitmap_data data;
        if (GdipBitmapLockBits(bitmap, &rect, GDIPLUS_LOCK_READ, GDIPLUS_PIXEL_FORMAT_32BPP_ARGB, &data) == 0) {
            pixels = malloc((size_t)width * height * sizeof *pixels);
            for (UINT row = 0; pixels && row < height; row++) {
                const uint8_t *source = (const uint8_t *)data.scan0 + (size_t)data.stride * (height - 1 - row);
                for (UINT column = 0; column < width; column++) {
                    pixels[(size_t)row * width + column] = encode_pixel(source + column * 4, header->pixel_format);
                }
            }
            GdipBitmapUnlockBits(bitmap, &data);
        }
    }
    GdipDisposeImage(bitmap);
    return pixels;
}

static int first_time_seen(uint32_t key) {
    uint32_t slot = (key * 2654435761u) % SEEN_SLOTS;
    for (int probe = 0; probe < SEEN_SLOTS; probe++, slot = (slot + 1) % SEEN_SLOTS) {
        if (seen_textures[slot] == key) return 0;
        if (!seen_textures[slot]) {
            seen_textures[slot] = key;
            return 1;
        }
    }
    return 0;
}

static void open_texture_log(const char *game_folder) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%stodmod_textures.log", game_folder);
    texture_log = fopen(path, "w");
    if (!texture_log) return;
    fputs("time\tbank\tindex\tid\twidth\theight\tpixel_format\tdata_format\treplacement\treplacement_width\t"
          "replacement_height\n",
          texture_log);
    fflush(texture_log);
    log_line("textures: logging every texture to %s", path);
}

static void write_texture_log(const char *bank_name, int index, const texture_header *header, const char *replacement,
                              UINT width, UINT height) {
    if (!texture_log) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(texture_log, "%02d:%02d:%02d.%03d\t%s\t%d\t%u\t%u\t%u\t%u\t%u\t%s\t%u\t%u\n", now.wHour, now.wMinute,
            now.wSecond, now.wMilliseconds, bank_name, index, header->id, header->width, header->height,
            header->pixel_format,
            header->data_format, replacement ? replacement : "", width, height);
    fflush(texture_log);
}

static void *__cdecl create_texture_hook(const texture_header *header, const void *pixels) {
    int bank = 0, index = -1;
    if (!header) return original_create_texture(header, pixels);
    int known_bank = find_texture(header, &bank, &index);
    char bank_name[BANK_NAME_LENGTH] = "?";
    if (known_bank) bank_folder_name(bank, bank_name, sizeof bank_name);
    int first_time = first_time_seen((uint32_t)bank << 16 | (uint32_t)(index & 0xFFFF) | 0x80000000u);
    char path[MAX_PATH];
    if (!find_replacement(known_bank ? bank_name : NULL, index, header->id, path, sizeof path)) {
        if (first_time) write_texture_log(bank_name, index, header, NULL, 0, 0);
        uint16_t *recoloured = known_bank && header->data_format == DATA_FORMAT_RECTANGLE
                                   ? blood_recolour(bank_name, index, header->width, header->height, header->pixel_format, pixels)
                                   : NULL;
        if (!recoloured) return original_create_texture(header, pixels);
        void *texture = original_create_texture(header, recoloured);
        free(recoloured);
        if (blood_is_paintball()) paintball_register_texture(texture);
        return texture;
    }
    UINT width = 0, height = 0;
    uint16_t *replacement = load_png_pixels(path, header, &width, &height);
    if (first_time) write_texture_log(bank_name, index, header, replacement ? path : NULL, width, height);
    if (!replacement) return original_create_texture(header, pixels);
    texture_header rectangle = *header;
    rectangle.data_format = DATA_FORMAT_RECTANGLE;
    rectangle.width = (uint16_t)width;
    rectangle.height = (uint16_t)height;
    void *texture = original_create_texture(&rectangle, replacement);
    free(replacement);
    if (first_time) log_line("textures: %s #%d (id %u) <- %s (%ux%u)", bank_name, index, header->id, path, width, height);
    return texture;
}

void install_texture_hooks(const char *game_folder, const char *ini_path) {
    snprintf(texture_folder, sizeof texture_folder, "%stextures\\", game_folder);
    read_pack_list(ini_path);
    log_textures = GetPrivateProfileIntA("debug", "log_textures", 0, ini_path);
    if (log_textures) open_texture_log(game_folder);
    if (pack_count == 0 && !log_textures && !blood_needs_recolouring()) {
        log_line("textures: no packs listed in [textures] packs");
        return;
    }
    for (int i = 0; i < pack_count; i++) {
        list_pack_banks(i);
        int before = id_override_count;
        char folder[MAX_PATH];
        snprintf(folder, sizeof folder, "%s%s", texture_folder, pack_names[i]);
        index_id_overrides(i, folder, 0);
        log_line("textures: pack %d = %s (%d banks, %d files named by texture id)", i + 1, pack_names[i],
                 pack_bank_count[i], id_override_count - before);
    }
    if (id_override_count) qsort(id_overrides, id_override_count, sizeof *id_overrides, compare_id_overrides);
    read_bank_table();
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    if (!hglna || !bank_range_count) return;
    void *target = (unsigned char *)hglna + (CREATE_TEXTURE_VA - HGLNA_PREFERRED_BASE);
    MH_STATUS status = MH_CreateHook(target, create_texture_hook, (void **)&original_create_texture);
    if (status == MH_OK) status = MH_EnableHook(target);
    log_line("textures: %d banks in Table.dll; hook create_texture at %p: %s", bank_range_count, target,
             MH_StatusToString(status));
}
