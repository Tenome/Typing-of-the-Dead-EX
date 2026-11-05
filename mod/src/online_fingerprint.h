#pragma once
#include <stdint.h>

#define FINGERPRINT_PARTS 6

void online_fingerprint(const char *ini_path, const char *version, uint32_t parts[FINGERPRINT_PARTS]);
const char *online_fingerprint_part_name(int part);
