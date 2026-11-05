#pragma once
void install_typing_stats(const char *game_folder, const char *ini_path);
void typing_stats_frame(void);
void typing_stats_flush(void);
void typing_stats_finish(void);

typedef void (*typing_word_listener)(int player, const unsigned char *word, const char *typed, int typos);

typedef struct {
    int words, keys, typos, streak, best_streak;
    double wpm, accuracy;
} typing_stats_summary;

void typing_stats_on_word(typing_word_listener listener);
int typing_stats_live(int player, typing_stats_summary *out);
unsigned typing_stats_last_word_frames(int player);
