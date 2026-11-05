#pragma once
#include <windows.h>
#include "ini_file.h"

void mutators_page_create(HWND dialog, RECT area, const ini_file *ini);
void mutators_page_show(int visible);
void mutators_page_save(ini_file *ini);
