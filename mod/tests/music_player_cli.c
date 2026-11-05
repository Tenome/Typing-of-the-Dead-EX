#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "music_player.h"

void log_line(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    puts("");
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        puts("usage: music_player_cli <file.wav> <loop start> <loop end> <seconds>");
        return 1;
    }
    uint64_t frames = 0;
    printf("rate %d, frames %llu\n", music_player_wave_rate(argv[1], &frames), (unsigned long long)frames);
    printf("play: %d\n", music_player_play(argv[1], 1, _strtoui64(argv[2], NULL, 10), _strtoui64(argv[3], NULL, 10)));
    Sleep((DWORD)(atoi(argv[4]) * 1000));
    printf("active before stop: %d\n", music_player_active());
    music_player_stop();
    return 0;
}
