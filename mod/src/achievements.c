#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "achievements.h"
#include "achievement_list.h"
#include "typing_stats.h"
#include "subtitle_hooks.h"
#include "frame_hooks.h"
#include "log.h"

#define GAME_MODE (*(volatile int *)0x00a3e308)
#define GAMEPLAY_MODE 6
#define PLAYER_SCORE(p) (*(volatile int *)(0x00a18758 + (p) * 0x210))
#define TOAST_FRAMES 300
#define MARATHON_FRAMES (60 * 60 * 30)
#define SCREEN_2D_HEIGHT 480.0f
#define WORD_SLOT_FRAMES 60

static char achievements_path[MAX_PATH], history_path[MAX_PATH];
static unsigned char unlocked[ACHIEVEMENT_COUNT];
static int enabled, lifetime_words_before, toast_index = -1, toast_frames;
static unsigned play_frames;
static HFONT toast_font;
static int toast_font_height;
static int pending_toasts[ACHIEVEMENT_COUNT], pending_count;

static int achievement_index(const char *id) {
    for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
        if (!strcmp(achievement_list[i].id, id)) return i;
    }
    return -1;
}

static void load_unlocked(void) {
    FILE *file = fopen(achievements_path, "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof line, file)) {
        line[strcspn(line, "\t\r\n")] = '\0';
        int index = achievement_index(line);
        if (index >= 0) unlocked[index] = 1;
    }
    fclose(file);
}

static void read_lifetime_words(void) {
    FILE *file = fopen(history_path, "r");
    if (!file) return;
    char line[512];
    while (fgets(line, sizeof line, file)) {
        char *field = strchr(line, '\t');
        field = field ? strchr(field + 1, '\t') : NULL;
        if (field) lifetime_words_before += atoi(field + 1);
    }
    fclose(file);
}

void achievements_unlock(const char *id) {
    if (!enabled) return;
    int index = achievement_index(id);
    if (index < 0 || unlocked[index]) return;
    unlocked[index] = 1;
    FILE *file = fopen(achievements_path, "a");
    if (file) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        fprintf(file, "%s\t%04d-%02d-%02d %02d:%02d\t%s\n", id, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                achievement_list[index].name);
        fclose(file);
    }
    if (pending_count < ACHIEVEMENT_COUNT) pending_toasts[pending_count++] = index;
    log_line("achievement: %s", achievement_list[index].name);
}

static void check_word(int player, const unsigned char *word, const char *typed, int typos) {
    (void)word;
    typing_stats_summary stats;
    if (!typing_stats_live(player, &stats)) return;
    achievements_unlock("first_word");
    if (stats.words >= 100) achievements_unlock("words_100");
    if (stats.words >= 500) achievements_unlock("words_500");
    if (lifetime_words_before + stats.words >= 1000) achievements_unlock("lifetime_1000");
    if (lifetime_words_before + stats.words >= 10000) achievements_unlock("lifetime_10000");
    if (stats.streak >= 25) achievements_unlock("streak_25");
    if (stats.streak >= 100) achievements_unlock("streak_100");
    if (stats.words >= 30 && stats.wpm >= 60) achievements_unlock("wpm_60");
    if (stats.words >= 30 && stats.wpm >= 100) achievements_unlock("wpm_100");
    if (typos >= 10) achievements_unlock("butterfingers");
    if (strlen(typed) >= 8 && typing_stats_last_word_frames(player) < WORD_SLOT_FRAMES) achievements_unlock("lightning");
    SYSTEMTIME now;
    GetLocalTime(&now);
    if (now.wHour < 4) achievements_unlock("night_owl");
}

static void check_frame(void) {
    if (GAME_MODE != GAMEPLAY_MODE) return;
    play_frames++;
    if (play_frames >= MARATHON_FRAMES) achievements_unlock("marathon");
    int best = PLAYER_SCORE(0) > PLAYER_SCORE(1) ? PLAYER_SCORE(0) : PLAYER_SCORE(1);
    if (best >= 3000) achievements_unlock("score_3000");
    if (best >= 10000) achievements_unlock("score_10000");
}

void achievements_finish(void) {
    if (!enabled) return;
    for (int player = 0; player < 2; player++) {
        typing_stats_summary stats;
        if (typing_stats_live(player, &stats) && stats.words >= 100 && stats.accuracy >= 99.0) achievements_unlock("accuracy_99");
    }
}

static int draw_toast(HDC dc, int width, int height) {
    if (toast_frames <= 0 && pending_count) {
        toast_index = pending_toasts[0];
        memmove(pending_toasts, pending_toasts + 1, (size_t)(--pending_count) * sizeof pending_toasts[0]);
        toast_frames = TOAST_FRAMES;
    }
    if (toast_frames <= 0) return 0;
    if (!dc) return 1;
    toast_frames--;
    int em = (int)(18.0f * (float)height / SCREEN_2D_HEIGHT + 0.5f);
    if (!toast_font || toast_font_height != em) {
        if (toast_font) DeleteObject(toast_font);
        toast_font = CreateFontA(-em, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
        toast_font_height = em;
    }
    HGDIOBJ previous = SelectObject(dc, toast_font);
    SetBkMode(dc, TRANSPARENT);
    char lines[2][160];
    snprintf(lines[0], sizeof lines[0], "ACHIEVEMENT: %s", achievement_list[toast_index].name);
    snprintf(lines[1], sizeof lines[1], "%s", achievement_list[toast_index].description);
    int y = height / 8;
    for (int i = 0; i < 2; i++) {
        SIZE size;
        GetTextExtentPoint32A(dc, lines[i], (int)strlen(lines[i]), &size);
        int x = (width - size.cx) / 2;
        SetTextColor(dc, RGB(0, 0, 0));
        for (int d = -2; d <= 2; d += 2) {
            TextOutA(dc, x + d, y, lines[i], (int)strlen(lines[i]));
            TextOutA(dc, x, y + d, lines[i], (int)strlen(lines[i]));
        }
        SetTextColor(dc, i ? RGB(235, 235, 235) : RGB(255, 200, 40));
        TextOutA(dc, x, y, lines[i], (int)strlen(lines[i]));
        y += em + 6;
    }
    SelectObject(dc, previous);
    return 1;
}

void install_achievements(const char *game_folder, const char *ini_path) {
    if (!GetPrivateProfileIntA("stats", "achievements", 1, ini_path) || !GetPrivateProfileIntA("stats", "enabled", 1, ini_path))
        return;
    int test = GetPrivateProfileIntA("debug", "achievements_test", 0, ini_path);
    if (GetPrivateProfileIntA("debug", "autotype", 0, ini_path) && !test) {
        log_line("achievements: off while [debug] autotype is on");
        return;
    }
    enabled = 1;
    snprintf(achievements_path, sizeof achievements_path, "%sstats\\%s", game_folder,
             test ? "achievements_test.tsv" : "achievements.tsv");
    snprintf(history_path, sizeof history_path, "%sstats\\history.tsv", game_folder);
    load_unlocked();
    read_lifetime_words();
    typing_stats_on_word(check_word);
    frame_hooks_on_frame_end(check_frame);
    overlay_add(draw_toast);
    int count = 0;
    for (int i = 0; i < ACHIEVEMENT_COUNT; i++) count += unlocked[i];
    log_line("achievements: %d of %d unlocked, %d words typed before this game", count, ACHIEVEMENT_COUNT,
             lifetime_words_before);
}
