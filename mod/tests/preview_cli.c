#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "game_models.h"

static void write_bmp(const char *path, const uint32_t *pixels, int width, int height) {
    FILE *file = fopen(path, "wb");
    if (!file) return;
    uint32_t image_size = (uint32_t)width * height * 4;
    uint8_t header[54] = {'B', 'M'};
    *(uint32_t *)(header + 2) = 54 + image_size;
    *(uint32_t *)(header + 10) = 54;
    *(uint32_t *)(header + 14) = 40;
    *(int32_t *)(header + 18) = width;
    *(int32_t *)(header + 22) = -height;
    *(uint16_t *)(header + 26) = 1;
    *(uint16_t *)(header + 28) = 32;
    *(uint32_t *)(header + 34) = image_size;
    fwrite(header, 1, sizeof header, file);
    fwrite(pixels, 4, (size_t)width * height, file);
    fclose(file);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        puts("usage: preview_cli <game folder with trailing \\> <out folder> <kind>...");
        return 1;
    }
    if (!game_models_open(argv[1])) {
        puts("cannot open the game files");
        return 2;
    }
    printf("%d kinds\n", game_models_kind_count());
    enum { WIDTH = 240, HEIGHT = 320 };
    uint32_t *pixels = malloc(WIDTH * HEIGHT * 4);
    for (int i = 3; i < argc; i++) {
        int kind = atoi(argv[i]);
        int triangles = game_models_render_character(kind, pixels, WIDTH, HEIGHT, 0.0f);
        char path[512];
        snprintf(path, sizeof path, "%s\\kind_%d.bmp", argv[2], kind);
        write_bmp(path, pixels, WIDTH, HEIGHT);
        printf("kind %d: %d triangles\n", kind, triangles);
    }
    game_models_close();
    return 0;
}
