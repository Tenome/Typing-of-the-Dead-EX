#pragma once
#include <stddef.h>

#define SUBTITLE_LINE_IDS 401
#define SUBTITLE_MAX_PARTS 4
#define SUBTITLE_FRAMES_PER_SECOND 60
#define SUBTITLE_NO_TIME (-1)
#define SUBTITLE_LINE_TABLE_VA 0x0051cfd0
#define SUBTITLE_LINE_STRIDE 16
#define SUBTITLE_LINE_DURATION 2
#define SUBTITLE_LINE_SOUND 0xc
#define SUBTITLE_PARTS_VA 0x00524a28
#define SUBTITLE_RECORDS_VA 0x0051eea8
#define SUBTITLE_RECORD_STRIDE 0x40
#define SUBTITLE_RECORD_SWITCH 0x3e
#define SUBTITLE_VOICE_NAMES_VA 0x0053b1a4
#define SUBTITLE_VOICE_NAME_STRIDE 0x24
#define SUBTITLE_SOUND_KIND_VOICE 2

typedef int (*subtitle_read_fn)(void *context, unsigned va, void *out, unsigned size);

typedef struct {
    short duration;
    unsigned sound;
    short parts[SUBTITLE_MAX_PARTS];
    int part_count;
    char voice_name[SUBTITLE_VOICE_NAME_STRIDE + 1];
} subtitle_line;

typedef struct {
    subtitle_line lines[SUBTITLE_LINE_IDS];
    short record_switch[512];
    int record_count;
} subtitle_tables;

typedef struct {
    short start[SUBTITLE_MAX_PARTS];
    short end[SUBTITLE_MAX_PARTS];
    short total;
} subtitle_line_timing;

int subtitle_tables_read(subtitle_tables *tables, int record_count, subtitle_read_fn read, void *context);
void subtitle_game_timing(const subtitle_tables *tables, int line, subtitle_line_timing *timing);
int subtitle_line_of_record(const subtitle_tables *tables, int record, int *part);
int subtitle_has_voice(const subtitle_tables *tables, int line);
int subtitle_wave_frames(const char *path);
int subtitle_parse_seconds(const char *text);
void subtitle_format_seconds(int frames, char *out, size_t size);
void subtitle_apply_timing(const subtitle_tables *tables, int line, const short *record_start, const short *record_end,
                           int voice_frames, subtitle_line_timing *timing);
