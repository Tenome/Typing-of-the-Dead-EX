#pragma once
#include <stdint.h>

#define DETERMINISM_HASH_PARTS 4
#define REPORT_SIZE 7
#define SAVE_SIZE 0x22ae8

typedef void (*input_callback)(void);
typedef int (*save_loader)(unsigned char *buffer);
typedef void (*save_observer)(const unsigned char *buffer, int loaded);

void install_determinism(const char *ini_path, const char *log_suffix);
int determinism_installed(void);
void determinism_on_input(input_callback callback);
void determinism_set_save_loader(save_loader loader);
void determinism_block_save_writes(void);
void determinism_on_save_loaded(save_observer observer);
void determinism_hash(uint32_t parts[DETERMINISM_HASH_PARTS]);
unsigned char *determinism_device_report(int device);
uint32_t determinism_input_frame(void);
int determinism_read_save_file(unsigned char *buffer);
unsigned determinism_game_thread(void);
