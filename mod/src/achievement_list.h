#pragma once

typedef struct {
    const char *id;
    const char *name;
    const char *description;
} achievement_info;

static const achievement_info achievement_list[] = {
    {"first_word", "First Blood", "Type your first word."},
    {"words_100", "Warming Up", "Type 100 words in one game."},
    {"words_500", "Paperwork", "Type 500 words in one game."},
    {"lifetime_1000", "Thousand Words", "Type 1,000 words in all."},
    {"lifetime_10000", "Novelist", "Type 10,000 words in all."},
    {"streak_25", "Steady Hands", "Type 25 words in a row without a typo."},
    {"streak_100", "Flawless", "Type 100 words in a row without a typo."},
    {"wpm_60", "Quick Fingers", "Reach 60 words per minute in a game (after 30 words)."},
    {"wpm_100", "Machine Gun Keyboard", "Reach 100 words per minute in a game (after 30 words)."},
    {"accuracy_99", "Surgeon", "Finish a game of 100+ words with 99% accuracy or better."},
    {"lightning", "Lightning", "Type a word of 8 or more letters in under a second."},
    {"butterfingers", "Butterfingers", "Make 10 typos on a single word."},
    {"score_3000", "High Scorer", "Reach 3,000 points during play."},
    {"score_10000", "Legend of the Keyboard", "Reach 10,000 points during play."},
    {"marathon", "Marathon", "Play for 30 minutes in one sitting."},
    {"night_owl", "Night Owl", "Type a word between midnight and 4 a.m."},
    {"photographer", "Photographer", "Save a photo in photo mode."},
};

#define ACHIEVEMENT_COUNT ((int)(sizeof achievement_list / sizeof achievement_list[0]))
