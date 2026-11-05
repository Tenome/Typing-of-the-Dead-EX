#pragma once
#include <wchar.h>

enum { WORD_ORDER_NORMAL, WORD_ORDER_REVERSED, WORD_ORDER_SCRAMBLED };
enum { LETTER_CASE_NORMAL, LETTER_CASE_UPPER, LETTER_CASE_LOWER, LETTER_CASE_RANDOM };

extern int word_mutator_order;
extern int word_mutator_case;

void read_word_mutators(const char *ini_path);
int word_mutators_active(void);
void mutate_word_text(wchar_t *display, int display_length, wchar_t *reading, int reading_length);
