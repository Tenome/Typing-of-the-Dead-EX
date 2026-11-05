#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "subtitle_timing.h"

int subtitle_tables_read(subtitle_tables *tables, int record_count, subtitle_read_fn read, void *context) {
    memset(tables, 0, sizeof *tables);
    tables->record_count = record_count;
    for (int line = 0; line < SUBTITLE_LINE_IDS; line++) {
        subtitle_line *entry = &tables->lines[line];
        unsigned base = SUBTITLE_LINE_TABLE_VA + line * SUBTITLE_LINE_STRIDE;
        if (!read(context, base + SUBTITLE_LINE_DURATION, &entry->duration, 2) ||
            !read(context, base + SUBTITLE_LINE_SOUND, &entry->sound, 4) ||
            !read(context, SUBTITLE_PARTS_VA + line * SUBTITLE_MAX_PARTS * 2, entry->parts, sizeof entry->parts))
            return 0;
        while (entry->part_count < SUBTITLE_MAX_PARTS && entry->parts[entry->part_count] >= 0 &&
               entry->parts[entry->part_count] < record_count)
            entry->part_count++;
        if (entry->sound >> 28 == SUBTITLE_SOUND_KIND_VOICE &&
            !read(context, SUBTITLE_VOICE_NAMES_VA + (entry->sound & 0xfff) * SUBTITLE_VOICE_NAME_STRIDE, entry->voice_name,
                  SUBTITLE_VOICE_NAME_STRIDE))
            entry->voice_name[0] = '\0';
        entry->voice_name[SUBTITLE_VOICE_NAME_STRIDE] = '\0';
    }
    for (int record = 0; record < record_count && record < (int)(sizeof tables->record_switch / 2); record++) {
        if (!read(context, SUBTITLE_RECORDS_VA + record * SUBTITLE_RECORD_STRIDE + SUBTITLE_RECORD_SWITCH,
                  &tables->record_switch[record], 2))
            return 0;
    }
    return 1;
}

int subtitle_has_voice(const subtitle_tables *tables, int line) {
    return line >= 0 && line < SUBTITLE_LINE_IDS && tables->lines[line].voice_name[0] &&
           tables->lines[line].sound >> 28 == SUBTITLE_SOUND_KIND_VOICE;
}

void subtitle_game_timing(const subtitle_tables *tables, int line, subtitle_line_timing *timing) {
    const subtitle_line *entry = &tables->lines[line];
    timing->total = entry->duration;
    int start = 0;
    for (int part = 0; part < SUBTITLE_MAX_PARTS; part++) {
        timing->start[part] = timing->end[part] = SUBTITLE_NO_TIME;
        if (part >= entry->part_count) continue;
        timing->start[part] = (short)start;
        int next = part + 1 < entry->part_count ? entry->duration - tables->record_switch[entry->parts[part]] : entry->duration;
        if (next < start) next = start;
        if (next > entry->duration) next = entry->duration;
        timing->end[part] = (short)next;
        start = next;
    }
}

int subtitle_line_of_record(const subtitle_tables *tables, int record, int *part) {
    for (int line = 0; line < SUBTITLE_LINE_IDS; line++) {
        for (int i = 0; i < tables->lines[line].part_count; i++) {
            if (tables->lines[line].parts[i] != record) continue;
            if (part) *part = i;
            return line;
        }
    }
    return -1;
}

void subtitle_apply_timing(const subtitle_tables *tables, int line, const short *record_start, const short *record_end,
                           int voice_frames, subtitle_line_timing *timing) {
    const subtitle_line *entry = &tables->lines[line];
    subtitle_game_timing(tables, line, timing);
    int count = entry->part_count;
    for (int part = 0; part < count; part++) {
        int record = entry->parts[part];
        if (record_start && record_start[record] != SUBTITLE_NO_TIME) timing->start[part] = record_start[record];
        if (record_end && record_end[record] != SUBTITLE_NO_TIME) {
            timing->end[part] = record_end[record];
        } else if (part + 1 < count && record_start && record_start[entry->parts[part + 1]] != SUBTITLE_NO_TIME) {
            timing->end[part] = record_start[entry->parts[part + 1]];
        }
    }
    for (int part = 1; part < count; part++) {
        int record = entry->parts[part];
        int moved = record_start && record_start[record] != SUBTITLE_NO_TIME;
        if (!moved && record_end && record_end[entry->parts[part - 1]] != SUBTITLE_NO_TIME &&
            timing->start[part] < timing->end[part - 1])
            timing->start[part] = timing->end[part - 1];
        if (timing->start[part] < timing->start[part - 1]) timing->start[part] = timing->start[part - 1];
        if (timing->end[part - 1] > timing->start[part]) timing->end[part - 1] = timing->start[part];
    }
    for (int part = 0; part < count; part++) {
        if (timing->end[part] < timing->start[part] + 1) timing->end[part] = (short)(timing->start[part] + 1);
    }
    if (count && voice_frames > timing->end[count - 1]) {
        int last_record = entry->parts[count - 1];
        if (!record_end || record_end[last_record] == SUBTITLE_NO_TIME) timing->end[count - 1] = (short)voice_frames;
    }
    timing->total = count ? timing->end[count - 1] : entry->duration;
    if (timing->total < 1) timing->total = 1;
}

int subtitle_parse_seconds(const char *text) {
    while (text && (*text == ' ' || *text == '\t')) text++;
    if (!text || (*text < '0' || *text > '9') && *text != '.') return SUBTITLE_NO_TIME;
    double seconds = atof(text);
    if (seconds < 0 || seconds > 500) return SUBTITLE_NO_TIME;
    return (int)(seconds * SUBTITLE_FRAMES_PER_SECOND + 0.5);
}

void subtitle_format_seconds(int frames, char *out, size_t size) {
    if (frames == SUBTITLE_NO_TIME) {
        if (size) out[0] = '\0';
        return;
    }
    snprintf(out, size, "%.2f", frames / (double)SUBTITLE_FRAMES_PER_SECOND);
}

int subtitle_wave_frames(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    unsigned char header[12];
    unsigned byte_rate = 0, data_bytes = 0;
    if (fread(header, 1, 12, file) == 12 && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVE", 4)) {
        unsigned char chunk[8];
        while (fread(chunk, 1, 8, file) == 8) {
            unsigned size = chunk[4] | chunk[5] << 8 | chunk[6] << 16 | (unsigned)chunk[7] << 24;
            if (!memcmp(chunk, "fmt ", 4)) {
                unsigned char format[16];
                if (size < 16 || fread(format, 1, 16, file) != 16) break;
                byte_rate = format[8] | format[9] << 8 | format[10] << 16 | (unsigned)format[11] << 24;
                fseek(file, (long)(size - 16 + (size & 1)), SEEK_CUR);
            } else if (!memcmp(chunk, "data", 4)) {
                data_bytes = size;
                break;
            } else {
                fseek(file, (long)(size + (size & 1)), SEEK_CUR);
            }
        }
    }
    fclose(file);
    return byte_rate ? (int)((unsigned long long)data_bytes * SUBTITLE_FRAMES_PER_SECOND / byte_rate) : 0;
}
