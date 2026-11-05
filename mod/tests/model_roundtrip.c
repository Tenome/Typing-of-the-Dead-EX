#include <stdio.h>
#include <stdarg.h>
#include "MinHook.h"
void log_line(const char *format, ...) { va_list a; va_start(a, format); vprintf(format, a); va_end(a); puts(""); }
#define MH_CreateHook(a, b, c) MH_OK
#define MH_EnableHook(a) MH_OK
#define MH_StatusToString(s) "test"
#include "../src/model_hooks.c"

int main(int argc, char **argv) {
    if (argc != 4) {
        puts("usage: model_roundtrip <model.bin> <out.obj> <rebuilt.bin>");
        return 1;
    }
    char *data = NULL;
    size_t size = 0;
    if (!read_text_file(argv[1], &data, &size)) return 1;
    const uint32_t *original = (const uint32_t *)data;
    printf("original object %zu bytes (file %zu)\n", object_size(original, size), size);
    write_object_obj(original, argv[2], "x.mtl");
    char *text = NULL;
    read_text_file(argv[2], &text, NULL);
    obj_mesh mesh = {0};
    if (!parse_obj(text, &mesh)) return 2;
    size_t built_size = 0;
    uint32_t *built = build_object_from_obj(original, &mesh, &built_size);
    printf("obj: %d positions, %d triangles, %d materials; rebuilt %zu bytes, walk %zu\n", mesh.position_count,
           mesh.corner_count / 3, mesh.material_count, built_size, object_size(built, built_size));
    FILE *out = fopen(argv[3], "wb");
    fwrite(built, 1, built_size, out);
    fclose(out);
    return 0;
}
