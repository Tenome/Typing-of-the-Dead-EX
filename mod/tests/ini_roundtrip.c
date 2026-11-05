#include <stdio.h>
#include "ini_file.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: ini_roundtrip in.ini unchanged.ini edited.ini\n");
        return 2;
    }
    ini_file ini;
    if (!ini_load(&ini, argv[1])) return 1;
    ini_save(&ini, argv[2]);
    printf("Window=%d Resolution=%d Bpp=%d\n", ini_get_bool(&ini, "HGLCORE", "Window", -1),
           ini_get_int(&ini, "HGLCORE", "Resolution", -1), ini_get_int(&ini, "HGLCORE", "Bpp", -1));
    ini_set_bool(&ini, "HGLCORE", "Window", 1);
    ini_set_int(&ini, "HGLCORE", "Resolution", 12);
    ini_set(&ini, "display", "width", "1920");
    ini_save(&ini, argv[3]);
    ini_free(&ini);
    return 0;
}
