#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "typewriter.h"
#include "typing_stats.h"
#include "voice_packs.h"
#include "log.h"

#define SAMPLE_RATE 22050
#define PI 3.14159265358979
#define AUTO_CHANNEL 0xffffffff
#define NO_SOUND_ID 0xffffffff

typedef BOOL(__cdecl *load_play_fn)(const char *path, int loop, unsigned channel, unsigned id);

static char clack_path[MAX_PATH], ding_path[MAX_PATH];
static load_play_fn load_play;
static unsigned noise_state = 0x1234567;

static double noise(void) {
    noise_state = noise_state * 1103515245u + 12345u;
    return ((noise_state >> 8) & 0xffff) / 32768.0 - 1.0;
}

static int write_wave(const char *path, const int16_t *samples, int count) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    uint32_t data_bytes = (uint32_t)count * 2, riff = 36 + data_bytes, rate = SAMPLE_RATE, byte_rate = SAMPLE_RATE * 2;
    uint32_t fmt_size = 16;
    uint16_t format = 1, channels = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, file);
    fwrite(&riff, 4, 1, file);
    fwrite("WAVEfmt ", 1, 8, file);
    fwrite(&fmt_size, 4, 1, file);
    fwrite(&format, 2, 1, file);
    fwrite(&channels, 2, 1, file);
    fwrite(&rate, 4, 1, file);
    fwrite(&byte_rate, 4, 1, file);
    fwrite(&align, 2, 1, file);
    fwrite(&bits, 2, 1, file);
    fwrite("data", 1, 4, file);
    fwrite(&data_bytes, 4, 1, file);
    fwrite(samples, 2, (size_t)count, file);
    fclose(file);
    return 1;
}

static int16_t clip(double value) {
    if (value > 1.0) value = 1.0;
    if (value < -1.0) value = -1.0;
    return (int16_t)(value * 32000);
}

static int make_clack(const char *path) {
    int count = SAMPLE_RATE * 9 / 100;
    int16_t *samples = malloc((size_t)count * 2);
    if (!samples) return 0;
    double low = 0;
    for (int i = 0; i < count; i++) {
        double t = (double)i / SAMPLE_RATE;
        double strike = noise() * exp(-t / 0.006);
        low += (noise() - low) * 0.15;
        double body = low * exp(-t / 0.02) * 1.6;
        double ring = sin(2 * PI * 2350 * t) * exp(-t / 0.012) * 0.35;
        double thump = sin(2 * PI * 140 * t) * exp(-t / 0.025) * 0.5;
        samples[i] = clip(0.8 * strike + body + ring + thump);
    }
    int ok = write_wave(path, samples, count);
    free(samples);
    return ok;
}

static int make_ding(const char *path) {
    int count = SAMPLE_RATE * 12 / 10;
    int16_t *samples = malloc((size_t)count * 2);
    if (!samples) return 0;
    for (int i = 0; i < count; i++) {
        double t = (double)i / SAMPLE_RATE;
        double bell = sin(2 * PI * 2093 * t) * exp(-t / 0.45) + 0.35 * sin(2 * PI * 5250 * t) * exp(-t / 0.18) +
                      0.2 * sin(2 * PI * 3520 * t) * exp(-t / 0.3);
        double strike = noise() * exp(-t / 0.004) * 0.3;
        samples[i] = clip(0.5 * bell + strike);
    }
    int ok = write_wave(path, samples, count);
    free(samples);
    return ok;
}

static const char *gunshot_as_clack(const char *game_path) {
    const char *name = strrchr(game_path, '/');
    name = name ? name + 1 : game_path;
    if (_strnicmp(game_path, "Sound/SE/", 9) != 0 || _strnicmp(name, "GUN", 3) != 0) return NULL;
    return clack_path;
}

static void ding_on_word(int player, const unsigned char *word, const char *typed, int typos) {
    (void)player;
    (void)word;
    (void)typed;
    (void)typos;
    if (load_play) load_play(ding_path, 0, AUTO_CHANNEL, NO_SOUND_ID);
}

void install_typewriter(const char *game_folder, const char *ini_path) {
    if (!GetPrivateProfileIntA("fun", "typewriter_gun", 0, ini_path)) return;
    char folder[MAX_PATH];
    snprintf(folder, sizeof folder, "%svoices", game_folder);
    CreateDirectoryA(folder, NULL);
    snprintf(folder, sizeof folder, "%svoices\\.cache", game_folder);
    CreateDirectoryA(folder, NULL);
    snprintf(clack_path, sizeof clack_path, "%s\\typewriter_clack.wav", folder);
    snprintf(ding_path, sizeof ding_path, "%s\\typewriter_ding.wav", folder);
    if (!make_clack(clack_path) || !make_ding(ding_path)) {
        log_line("typewriter: cannot write the sounds to %s", folder);
        return;
    }
    HMODULE hglna = GetModuleHandleA("HglNa.dll");
    load_play = hglna ? (load_play_fn)GetProcAddress(hglna, "nlsndLoadPlay") : NULL;
    voice_packs_add_redirect(gunshot_as_clack);
    typing_stats_on_word(ding_on_word);
    log_line("typewriter: gunshots clack, finished words ring the bell");
}
