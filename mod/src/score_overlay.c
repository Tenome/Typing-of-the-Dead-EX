#include <windows.h>
#include <stdio.h>
#include "score_overlay.h"
#include "subtitle_hooks.h"
#include "log.h"
#include "typing_stats.h"

#define GAME_MODE (*(volatile int *)0x00a3e308)
#define GAMEPLAY_MODE 6
#define PLAYER_STATE(p) (*(volatile short *)(0x00a18622 + (p) * 0x210))
#define PLAYER_NOT_PLAYING 9
#define PLAYER_SCORE(p) (*(volatile int *)(0x00a18758 + (p) * 0x210))
#define SCREEN_2D_HEIGHT 480.0f
#define FONT_HEIGHT_2D 20.0f
#define TOP_2D 10.0f
#define RIGHT_MARGIN_2D 12.0f

static HFONT font;
static int show_score, show_typing;
static int font_height;

static int score_visible(void) {
    return GAME_MODE == GAMEPLAY_MODE && (PLAYER_STATE(0) != PLAYER_NOT_PLAYING || PLAYER_STATE(1) != PLAYER_NOT_PLAYING);
}

static void draw_outlined_right(HDC dc, int right, int y, const char *text, COLORREF colour, int outline) {
    SIZE size;
    int length = (int)strlen(text);
    GetTextExtentPoint32A(dc, text, length, &size);
    int x = right - size.cx;
    SetTextColor(dc, RGB(0, 0, 0));
    for (int dy = -outline; dy <= outline; dy++) {
        for (int dx = -outline; dx <= outline; dx++) {
            if (dx || dy) TextOutA(dc, x + dx, y + dy, text, length);
        }
    }
    SetTextColor(dc, colour);
    TextOutA(dc, x, y, text, length);
}

static void log_score_when_play_ends(int visible) {
    static int was_visible, last_score[2];
    if (visible) {
        for (int p = 0; p < 2; p++) last_score[p] = PLAYER_SCORE(p);
    } else if (was_visible) {
        log_line("score: play ended (game mode %d) with player 1 %d, player 2 %d", GAME_MODE, last_score[0], last_score[1]);
    }
    was_visible = visible;
}

static int draw_score(HDC dc, int width, int height) {
    if (!dc) log_score_when_play_ends(score_visible());
    if (!score_visible()) return 0;
    if (!dc) return 1;
    int em = (int)(FONT_HEIGHT_2D * (float)height / SCREEN_2D_HEIGHT + 0.5f);
    if (!font || font_height != em) {
        if (font) DeleteObject(font);
        font = CreateFontA(-em, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
        font_height = em;
    }
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    float scale = (float)height / SCREEN_2D_HEIGHT;
    int right = width - (int)(RIGHT_MARGIN_2D * scale), y = (int)(TOP_2D * scale), outline = height >= 720 ? 2 : 1;
    int both = PLAYER_STATE(0) != PLAYER_NOT_PLAYING && PLAYER_STATE(1) != PLAYER_NOT_PLAYING;
    for (int p = 0; p < 2; p++) {
        if (PLAYER_STATE(p) == PLAYER_NOT_PLAYING) continue;
        char text[96];
        COLORREF colour = p ? RGB(120, 200, 255) : RGB(255, 230, 80);
        if (show_score) {
            if (both) snprintf(text, sizeof text, "%dP  SCORE %d", p + 1, PLAYER_SCORE(p));
            else snprintf(text, sizeof text, "SCORE %d", PLAYER_SCORE(p));
            draw_outlined_right(dc, right, y, text, colour, outline);
            y += em + 4;
        }
        typing_stats_summary typing;
        if (show_typing && typing_stats_live(p, &typing)) {
            snprintf(text, sizeof text, "%s%.0f WPM  %.1f%%  STREAK %d", both && !show_score ? (p ? "2P  " : "1P  ") : "",
                     typing.wpm, typing.accuracy, typing.streak);
            draw_outlined_right(dc, right, y, text, RGB(235, 235, 235), outline);
            y += em + 4;
        }
    }
    SelectObject(dc, previous);
    return 1;
}

void install_score_overlay(const char *ini_path) {
    show_score = GetPrivateProfileIntA("display", "show_score", 0, ini_path);
    show_typing = GetPrivateProfileIntA("display", "show_typing", 0, ini_path);
    if (!show_score && !show_typing) return;
    overlay_add(draw_score);
    log_line("score: %s%s%s shown in the top right during play", show_score ? "score" : "",
             show_score && show_typing ? " and " : "", show_typing ? "words per minute, accuracy and streak" : "");
}
