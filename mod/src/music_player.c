#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>
#include "music_player.h"
#include "log.h"
#include "volume.h"

#define BUFFER_COUNT 4
#define BUFFER_FRAMES 8192
#define MAX_CHANNELS 2
#define WAVE_TAG_PCM 1
#define WAVE_TAG_FLOAT 3
#define WAVE_TAG_EXTENSIBLE 0xFFFE

typedef struct {
    HANDLE file;
    int channels, sample_rate, bits, is_float;
    uint32_t data_offset;
    uint64_t frames;
    uint64_t loop_start, loop_end;
    uint64_t position;
    int loop;
    int loops_logged;
} music_stream;

static HANDLE player_thread;
static volatile LONG stop_requested;
static music_stream stream;

static int read_at(HANDLE file, uint64_t offset, void *buffer, DWORD size) {
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)offset;
    DWORD read = 0;
    return SetFilePointerEx(file, position, NULL, FILE_BEGIN) && ReadFile(file, buffer, size, &read, NULL) && read == size;
}

static int open_wave(const char *path, music_stream *out) {
    memset(out, 0, sizeof *out);
    out->file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out->file == INVALID_HANDLE_VALUE) return 0;
    uint8_t header[12];
    if (!read_at(out->file, 0, header, sizeof header) || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) return 0;
    uint64_t offset = 12;
    int have_format = 0;
    for (;;) {
        uint8_t chunk[8];
        if (!read_at(out->file, offset, chunk, sizeof chunk)) return 0;
        uint32_t size;
        memcpy(&size, chunk + 4, 4);
        if (!memcmp(chunk, "fmt ", 4) && size >= 16) {
            uint8_t format[40] = {0};
            if (!read_at(out->file, offset + 8, format, size < sizeof format ? size : sizeof format)) return 0;
            uint16_t tag, channels, bits;
            uint32_t rate;
            memcpy(&tag, format, 2);
            memcpy(&channels, format + 2, 2);
            memcpy(&rate, format + 4, 4);
            memcpy(&bits, format + 14, 2);
            if (tag == WAVE_TAG_EXTENSIBLE && size >= 26) memcpy(&tag, format + 24, 2);
            out->channels = channels;
            out->sample_rate = (int)rate;
            out->bits = bits;
            out->is_float = tag == WAVE_TAG_FLOAT;
            have_format = (tag == WAVE_TAG_PCM || tag == WAVE_TAG_FLOAT) && channels >= 1 && channels <= MAX_CHANNELS &&
                          (bits == 8 || bits == 16 || bits == 24 || bits == 32);
        } else if (!memcmp(chunk, "data", 4)) {
            if (!have_format) return 0;
            out->data_offset = (uint32_t)(offset + 8);
            out->frames = size / (uint32_t)(out->channels * out->bits / 8);
            return out->frames > 0;
        }
        offset += 8 + size + (size & 1);
    }
}

static int16_t sample_to_16(const uint8_t *sample, int bits, int is_float) {
    if (is_float) {
        float value;
        memcpy(&value, sample, 4);
        value = value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : value;
        return (int16_t)(value * 32767.0f);
    }
    switch (bits) {
    case 8: return (int16_t)((sample[0] - 128) << 8);
    case 16: return (int16_t)(sample[0] | sample[1] << 8);
    case 24: return (int16_t)(sample[1] | sample[2] << 8);
    default: return (int16_t)(sample[2] | sample[3] << 8);
    }
}

static int read_frames(music_stream *s, int16_t *out, int frames) {
    static uint8_t raw[BUFFER_FRAMES * MAX_CHANNELS * 4];
    int frame_bytes = s->channels * s->bits / 8;
    if (!read_at(s->file, s->data_offset + s->position * (uint64_t)frame_bytes, raw, (DWORD)(frames * frame_bytes))) return 0;
    for (int i = 0; i < frames * s->channels; i++) out[i] = sample_to_16(raw + i * (s->bits / 8), s->bits, s->is_float);
    s->position += (uint64_t)frames;
    return 1;
}

static int fill_buffer(music_stream *s, int16_t *out) {
    int filled = 0;
    while (filled < BUFFER_FRAMES) {
        uint64_t end = s->loop ? s->loop_end : s->frames;
        if (s->position >= end) {
            if (!s->loop) break;
            if (s->loops_logged < 3) {
                s->loops_logged++;
                log_line("music: loop %llu -> %llu", (unsigned long long)s->position, (unsigned long long)s->loop_start);
            }
            s->position = s->loop_start;
            continue;
        }
        uint64_t available = end - s->position;
        int frames = (int)(available < (uint64_t)(BUFFER_FRAMES - filled) ? available : (uint64_t)(BUFFER_FRAMES - filled));
        if (!read_frames(s, out + filled * s->channels, frames)) break;
        filled += frames;
    }
    volume_scale_samples(out, (size_t)filled * s->channels, volume_music_factor());
    return filled;
}

static volatile LONG pause_requested;

static DWORD WINAPI player_main(LPVOID unused) {
    (void)unused;
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, (WORD)stream.channels, (DWORD)stream.sample_rate,
                           (DWORD)(stream.sample_rate * stream.channels * 2), (WORD)(stream.channels * 2), 16, 0};
    HANDLE done = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEOUT output;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, (DWORD_PTR)done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        log_line("music: cannot open the sound output");
        CloseHandle(done);
        return 0;
    }
    static int16_t samples[BUFFER_COUNT][BUFFER_FRAMES * MAX_CHANNELS];
    WAVEHDR headers[BUFFER_COUNT];
    memset(headers, 0, sizeof headers);
    int finished = 0, paused = 0;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        headers[i].lpData = (LPSTR)samples[i];
        headers[i].dwFlags = WHDR_DONE;
    }
    while (!stop_requested) {
        if (pause_requested != paused) {
            paused = pause_requested;
            if (paused) waveOutPause(output);
            else waveOutRestart(output);
        }
        if (paused) {
            WaitForSingleObject(done, 50);
            continue;
        }
        int playing = 0;
        for (int i = 0; i < BUFFER_COUNT; i++) {
            if (!(headers[i].dwFlags & WHDR_DONE)) {
                playing++;
                continue;
            }
            if (headers[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(output, &headers[i], sizeof headers[i]);
            if (finished) continue;
            int frames = fill_buffer(&stream, samples[i]);
            if (!frames) {
                finished = 1;
                continue;
            }
            headers[i].dwBufferLength = (DWORD)(frames * stream.channels * 2);
            headers[i].dwFlags = 0;
            waveOutPrepareHeader(output, &headers[i], sizeof headers[i]);
            waveOutWrite(output, &headers[i], sizeof headers[i]);
            playing++;
        }
        if (finished && !playing) break;
        WaitForSingleObject(done, 100);
    }
    log_line("music: player ends (%s, position %llu)", stop_requested ? "stopped" : "track finished",
             (unsigned long long)stream.position);
    waveOutReset(output);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (headers[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(output, &headers[i], sizeof headers[i]);
    }
    waveOutClose(output);
    CloseHandle(done);
    return 0;
}

void music_player_stop(void) {
    if (!player_thread) return;
    InterlockedExchange(&stop_requested, 1);
    WaitForSingleObject(player_thread, 2000);
    CloseHandle(player_thread);
    player_thread = NULL;
    if (stream.file && stream.file != INVALID_HANDLE_VALUE) CloseHandle(stream.file);
    memset(&stream, 0, sizeof stream);
}

int music_player_wave_rate(const char *wave_path, uint64_t *frames) {
    music_stream probe;
    int ok = open_wave(wave_path, &probe);
    if (probe.file && probe.file != INVALID_HANDLE_VALUE) CloseHandle(probe.file);
    if (frames) *frames = ok ? probe.frames : 0;
    return ok ? probe.sample_rate : 0;
}

int music_player_play(const char *wave_path, int loop, uint64_t loop_start, uint64_t loop_end) {
    music_player_stop();
    if (!open_wave(wave_path, &stream)) {
        if (stream.file && stream.file != INVALID_HANDLE_VALUE) CloseHandle(stream.file);
        memset(&stream, 0, sizeof stream);
        return 0;
    }
    if (loop_end == 0 || loop_end > stream.frames) loop_end = stream.frames;
    if (loop_start >= loop_end) loop_start = 0;
    stream.loop = loop;
    stream.loop_start = loop_start;
    stream.loop_end = loop_end;
    InterlockedExchange(&stop_requested, 0);
    InterlockedExchange(&pause_requested, 0);
    player_thread = CreateThread(NULL, 0, player_main, NULL, 0, NULL);
    return player_thread != NULL;
}

void music_player_pause(int on) {
    InterlockedExchange(&pause_requested, on ? 1 : 0);
}

int music_player_active(void) {
    return player_thread != NULL;
}
