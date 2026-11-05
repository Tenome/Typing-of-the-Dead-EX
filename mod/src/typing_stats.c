#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "typing_stats.h"
#include "frame_hooks.h"
#include "log.h"
#include "replay.h"
#include "MinHook.h"

#define JUDGE_ROMAJI_KEY_VA 0x004cb5a0
#define JUDGE_KANA_KEY_VA 0x004cb710
#define INPUT_MODE (*(volatile char *)0x00547c1c)
#define ROMAJI_INPUT_MODE 'A'
#define GAME_FRAME (*(volatile uint32_t *)0x00a18fac)
#define GAME_MODE (*(volatile uint32_t *)0x00a3e308)
#define PLAYER_TYPING (-1)
#define RESULT_MISS 0x10
#define RESULT_WORD_DONE 0x80
#define WORD_ROMAJI_TEXT 8
#define WORD_ROMAJI_POSITION 0x49
#define MAX_ROMAJI 64
#define PLAYERS 2
#define MAX_WORDS 4096
#define LETTERS 128
#define FRAMES_PER_MINUTE 3600.0
#define REPORT_WORDS 15
#define FLUSH_FRAMES 3600

typedef uint8_t(__cdecl *judge_key_fn)(int *typing_state, int player, int mode);

typedef struct {
    char text[MAX_ROMAJI];
    uint32_t frames;
    uint16_t misses;
    uint8_t player;
} word_result;

typedef struct {
    const void *word;
    uint32_t start_frame;
    uint32_t last_frame;
    int misses;
    char text[MAX_ROMAJI];
} word_in_progress;

typedef struct {
    uint32_t letters, misses, words, typing_frames;
    uint32_t letter_misses[LETTERS], letter_counts[LETTERS];
} player_totals;

static judge_key_fn original_judge_romaji_key, original_judge_kana_key;
static player_totals totals[PLAYERS];
static word_in_progress current[PLAYERS];
static word_result *results;
static int result_count;
static char stats_folder[MAX_PATH];
static char session_name[64];
static uint32_t last_flush_frame;
static int dirty, test_session;
static typing_word_listener word_listeners[4];
static int word_listener_count;
static int clean_streak[PLAYERS], best_streak[PLAYERS];
static uint32_t last_word_frames[PLAYERS];

static void copy_romaji(const unsigned char *word, char *out) {
    int i = 0;
    for (; i < MAX_ROMAJI - 1 && word[WORD_ROMAJI_TEXT + i]; i++) {
        unsigned char character = word[WORD_ROMAJI_TEXT + i];
        out[i] = (char)(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
    }
    out[i] = '\0';
}

void typing_stats_on_word(typing_word_listener listener) {
    if (word_listener_count < 4) word_listeners[word_listener_count++] = listener;
}

static void finish_word(int player) {
    word_in_progress *word = &current[player];
    last_word_frames[player] = word->last_frame - word->start_frame + 1;
    clean_streak[player] = word->misses ? 0 : clean_streak[player] + 1;
    if (clean_streak[player] > best_streak[player]) best_streak[player] = clean_streak[player];
    for (int i = 0; i < word_listener_count; i++) word_listeners[i](player, (const unsigned char *)word->word, word->text, word->misses);
    uint32_t frames = word->last_frame - word->start_frame + 1;
    totals[player].words++;
    totals[player].typing_frames += frames;
    if (result_count < MAX_WORDS) {
        word_result *result = &results[result_count++];
        memcpy(result->text, word->text, sizeof result->text);
        result->frames = frames;
        result->misses = (uint16_t)word->misses;
        result->player = (uint8_t)player;
    }
    word->word = NULL;
}

static void record_key(int player, const unsigned char *word, unsigned char expected, uint8_t result) {
    if (player < 0 || player >= PLAYERS || !word || !result) return;
    uint32_t now = GAME_FRAME;
    word_in_progress *progress = &current[player];
    if (progress->word != word) {
        progress->word = word;
        progress->start_frame = now;
        progress->misses = 0;
        copy_romaji(word, progress->text);
    }
    progress->last_frame = now;
    if (expected >= 'a' && expected <= 'z') expected = (unsigned char)(expected - 'a' + 'A');
    if (expected < LETTERS) totals[player].letter_counts[expected]++;
    if (result & RESULT_MISS) {
        totals[player].misses++;
        progress->misses++;
        if (expected < LETTERS) totals[player].letter_misses[expected]++;
    } else {
        totals[player].letters++;
    }
    if (result & RESULT_WORD_DONE) finish_word(player);
    dirty = 1;
}

static uint8_t judge_and_record(judge_key_fn original, int *typing_state, int player, int mode) {
    const unsigned char *word = typing_state ? (const unsigned char *)(intptr_t)typing_state[0] : NULL;
    unsigned char expected = word ? word[WORD_ROMAJI_TEXT + word[WORD_ROMAJI_POSITION]] : 0;
    uint8_t result = original(typing_state, player, mode);
    if (mode == PLAYER_TYPING && word && !replay_playing()) record_key(player, word, expected, result);
    return result;
}

static uint8_t __cdecl judge_romaji_key_hook(int *typing_state, int player, int mode) {
    return judge_and_record(original_judge_romaji_key, typing_state, player, mode);
}

static uint8_t __cdecl judge_kana_key_hook(int *typing_state, int player, int mode) {
    if (INPUT_MODE == ROMAJI_INPUT_MODE) return original_judge_kana_key(typing_state, player, mode);
    return judge_and_record(original_judge_kana_key, typing_state, player, mode);
}

static double words_per_minute(const player_totals *player) {
    return player->typing_frames ? player->letters / 5.0 / (player->typing_frames / FRAMES_PER_MINUTE) : 0.0;
}

unsigned typing_stats_last_word_frames(int player) {
    return player >= 0 && player < PLAYERS ? last_word_frames[player] : 0;
}

int typing_stats_live(int player, typing_stats_summary *out) {
    if (!results || player < 0 || player >= PLAYERS) return 0;
    const player_totals *p = &totals[player];
    out->words = (int)p->words;
    out->keys = (int)p->letters;
    out->typos = (int)p->misses;
    out->wpm = words_per_minute(p);
    out->accuracy = p->letters + p->misses ? 100.0 * p->letters / (p->letters + p->misses) : 100.0;
    out->streak = clean_streak[player];
    out->best_streak = best_streak[player];
    return p->letters + p->misses > 0;
}

static int compare_slowest(const void *a, const void *b) {
    const word_result *left = a, *right = b;
    if (left->misses != right->misses) return left->misses < right->misses ? 1 : -1;
    double left_rate = (double)left->frames / (strlen(left->text) + 1);
    double right_rate = (double)right->frames / (strlen(right->text) + 1);
    return left_rate < right_rate ? 1 : left_rate > right_rate ? -1 : 0;
}

static void worst_letters(const player_totals *player, char *out, size_t size) {
    int order[LETTERS], count = 0;
    for (int letter = 0; letter < LETTERS; letter++) {
        if (player->letter_misses[letter]) order[count++] = letter;
    }
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (player->letter_misses[order[j]] > player->letter_misses[order[i]]) {
                int swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
        }
    }
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < count && i < 8 && used + 16 < size; i++) {
        used += (size_t)snprintf(out + used, size - used, "%s%c x%u", i ? ", " : "", order[i],
                                 player->letter_misses[order[i]]);
    }
}

static void write_session_report(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s%s.txt", stats_folder, session_name);
    FILE *file = fopen(path, "w");
    if (!file) return;
    word_result *sorted = malloc((size_t)(result_count ? result_count : 1) * sizeof *sorted);
    for (int player = 0; player < PLAYERS; player++) {
        const player_totals *p = &totals[player];
        if (!p->letters && !p->misses) continue;
        char letters[256];
        worst_letters(p, letters, sizeof letters);
        double accuracy = 100.0 * p->letters / (p->letters + p->misses);
        fprintf(file, "Player %d\n", player + 1);
        fprintf(file, "  words typed      %u\n", p->words);
        fprintf(file, "  correct keys     %u\n", p->letters);
        fprintf(file, "  typos            %u\n", p->misses);
        fprintf(file, "  accuracy         %.1f %%\n", accuracy);
        fprintf(file, "  speed            %.1f words per minute (5 keys = 1 word, time spent on words)\n",
                words_per_minute(p));
        fprintf(file, "  most missed      %s\n", letters[0] ? letters : "-");
        int count = 0;
        for (int i = 0; i < result_count; i++) {
            if (results[i].player == player) sorted[count++] = results[i];
        }
        qsort(sorted, (size_t)count, sizeof *sorted, compare_slowest);
        fprintf(file, "  hardest words (most typos first, then slowest per letter; seconds, typos):\n");
        for (int i = 0; i < count && i < REPORT_WORDS; i++)
            fprintf(file, "    %-24s %5.2f s  %u\n", sorted[i].text, sorted[i].frames / 60.0, sorted[i].misses);
        fprintf(file, "\n");
    }
    fprintf(file, "Every word (player, word, seconds, typos):\n");
    for (int i = 0; i < result_count; i++)
        fprintf(file, "%d\t%s\t%.2f\t%u\n", results[i].player + 1, results[i].text, results[i].frames / 60.0, results[i].misses);
    fclose(file);
    free(sorted);
}

static void write_history_line(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%shistory.tsv", stats_folder);
    int exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    FILE *file = fopen(path, "a");
    if (!file) return;
    if (!exists) fprintf(file, "session\tplayer\twords\tcorrect_keys\ttypos\taccuracy\twpm\tmost_missed\n");
    for (int player = 0; player < PLAYERS; player++) {
        const player_totals *p = &totals[player];
        if (!p->letters && !p->misses) continue;
        char letters[256];
        worst_letters(p, letters, sizeof letters);
        fprintf(file, "%s\t%d\t%u\t%u\t%u\t%.1f\t%.1f\t%s\n", session_name, player + 1, p->words, p->letters, p->misses,
                100.0 * p->letters / (p->letters + p->misses), words_per_minute(p), letters);
    }
    fclose(file);
}

static void write_practice_list(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%spractice_words.txt", stats_folder);
    FILE *file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "# Your most mistyped and slowest words from the last game. Make a word pack from this file with\n"
                  "# todmod_config.exe \"New pack from word list...\".\n");
    word_result *sorted = malloc((size_t)(result_count ? result_count : 1) * sizeof *sorted);
    memcpy(sorted, results, (size_t)result_count * sizeof *sorted);
    qsort(sorted, (size_t)result_count, sizeof *sorted, compare_slowest);
    int written = 0;
    for (int i = 0; i < result_count && written < 100; i++) {
        int duplicate = 0;
        for (int j = 0; j < i && !duplicate; j++) duplicate = !strcmp(sorted[j].text, sorted[i].text);
        if (duplicate || strlen(sorted[i].text) < 2) continue;
        fprintf(file, "%s\n", sorted[i].text);
        written++;
    }
    free(sorted);
    fclose(file);
}

void typing_stats_flush(void) {
    if (!results || !dirty || test_session) return;
    write_session_report();
    last_flush_frame = GAME_FRAME;
    dirty = 0;
}

void typing_stats_finish(void) {
    if (!results) return;
    for (int player = 0; player < PLAYERS; player++) {
        if (current[player].word) current[player].word = NULL;
    }
    if (!result_count && !totals[0].letters && !totals[1].letters) return;
    if (test_session) {
        log_line("stats: autotype game, nothing written to stats\\");
        return;
    }
    write_session_report();
    write_history_line();
    write_practice_list();
    for (int player = 0; player < PLAYERS; player++) {
        if (totals[player].letters || totals[player].misses)
            log_line("stats: player %d typed %u words, %u keys, %u typos, %.1f wpm (%sstats\\%s.txt)", player + 1,
                     totals[player].words, totals[player].letters, totals[player].misses, words_per_minute(&totals[player]),
                     "", session_name);
    }
}

void typing_stats_frame(void) {
    if (dirty && GAME_FRAME - last_flush_frame > FLUSH_FRAMES) typing_stats_flush();
}

static MH_STATUS hook_address(DWORD address, void *hook, void **original) {
    MH_STATUS status = MH_CreateHook((void *)address, hook, original);
    return status == MH_OK ? MH_EnableHook((void *)address) : status;
}

void install_typing_stats(const char *game_folder, const char *ini_path) {
    if (!GetPrivateProfileIntA("stats", "enabled", 1, ini_path)) return;
    results = calloc(MAX_WORDS, sizeof *results);
    test_session = GetPrivateProfileIntA("debug", "autotype", 0, ini_path);
    if (!results) return;
    snprintf(stats_folder, sizeof stats_folder, "%sstats\\", game_folder);
    CreateDirectoryA(stats_folder, NULL);
    SYSTEMTIME now;
    GetLocalTime(&now);
    snprintf(session_name, sizeof session_name, "%04d-%02d-%02d_%02d%02d%02d", now.wYear, now.wMonth, now.wDay, now.wHour,
             now.wMinute, now.wSecond);
    frame_hooks_on_frame_end(typing_stats_frame);
    log_line("stats: typing statistics to %s (hooks judge_romaji %s, judge_kana %s)", stats_folder,
             MH_StatusToString(hook_address(JUDGE_ROMAJI_KEY_VA, judge_romaji_key_hook, (void **)&original_judge_romaji_key)),
             MH_StatusToString(hook_address(JUDGE_KANA_KEY_VA, judge_kana_key_hook, (void **)&original_judge_kana_key)));
}
