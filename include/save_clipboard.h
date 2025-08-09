#pragma once
// Блок: Чтение буфера обмена и сохранение
#include <wchar.h>

void check_clipboard_and_save(wchar_t** last_text);
int  is_h_file(const char* text);
int  is_c_file(const char* text);
