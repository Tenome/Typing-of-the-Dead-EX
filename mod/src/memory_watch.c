#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_watch.h"
#include "frame_hooks.h"
#include "log.h"

#define MAX_CHANGES 12
#define GAME_FRAME (*(volatile unsigned *)0x00a18fac)

typedef struct {
    unsigned short count;
    unsigned frames[MAX_CHANGES];
    unsigned char values[MAX_CHANGES];
} byte_history;

static unsigned char *watch_start;
static size_t watch_size;
static unsigned char *previous;
static byte_history *history;
static char output_path[MAX_PATH];
static unsigned frames_watched;

static void watch_frame(void) {
    unsigned now = GAME_FRAME;
    for (size_t i = 0; i < watch_size; i++) {
        unsigned char value = watch_start[i];
        if (value == previous[i]) continue;
        previous[i] = value;
        byte_history *entry = &history[i];
        if (entry->count < MAX_CHANGES) {
            entry->frames[entry->count] = now;
            entry->values[entry->count] = value;
        }
        if (entry->count < 0xffff) entry->count++;
    }
    if (++frames_watched % 600 == 0) memory_watch_write();
}

void memory_watch_write(void) {
    if (!history) return;
    FILE *file = fopen(output_path, "w");
    if (!file) return;
    fprintf(file, "address\tchanges\tfirst_value\tframe:value...\n");
    for (size_t i = 0; i < watch_size; i++) {
        const byte_history *entry = &history[i];
        if (!entry->count || entry->count > MAX_CHANGES) continue;
        fprintf(file, "%08X\t%u\t", (unsigned)(uintptr_t)(watch_start + i), entry->count);
        for (int j = 0; j < entry->count; j++) fprintf(file, " %u:%u", entry->frames[j], entry->values[j]);
        fputc('\n', file);
    }
    fclose(file);
}

void install_memory_watch(const char *game_folder, const char *ini_path) {
    char range[64] = "";
    GetPrivateProfileStringA("debug", "memory_watch", "", range, sizeof range, ini_path);
    if (!range[0]) return;
    char *dash = strchr(range, '-');
    if (!dash) return;
    *dash = '\0';
    uintptr_t first = strtoul(range, NULL, 16), last = strtoul(dash + 1, NULL, 16);
    if (last <= first) return;
    watch_start = (unsigned char *)first;
    watch_size = last - first;
    previous = malloc(watch_size);
    history = calloc(watch_size, sizeof *history);
    if (!previous || !history) return;
    memcpy(previous, watch_start, watch_size);
    snprintf(output_path, sizeof output_path, "%stodmod_memory_watch.tsv", game_folder);
    frame_hooks_on_frame_end(watch_frame);
    log_line("debug: watching memory %08X-%08X for rarely changing bytes (%s)", (unsigned)first, (unsigned)last,
             output_path);
}
