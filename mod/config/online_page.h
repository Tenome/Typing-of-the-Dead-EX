#pragma once
#include <windows.h>
#include "ini_file.h"

void online_page_create(HWND dialog, RECT area, const ini_file *ini);
void online_page_show(int visible);
void online_page_save(ini_file *ini);
int online_page_message(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
