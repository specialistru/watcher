#pragma once
// Блок: Безопасные строки и утилиты
#include <stddef.h>

size_t z_strnlen(const char* s, size_t max);
void   str_copy_trunc(char* dst,size_t cap,const char* src);
void   z_buf_copy(char* dst,size_t cap,const char* src);
void   z_buf_cat(char* dst,size_t cap,const char* src);
void   z_buf_cat_char(char* dst,size_t cap,char ch);
int    istarts_with(const char* s,const char* prefix);
void   clear_eol(void);
