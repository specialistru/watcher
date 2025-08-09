#pragma once
// Блок: Имена файлов и извлечение базового имени
#include <stddef.h>

void to_lowercase_and_sanitize(char* s);
void strip_common_suffixes(char* base);
void extract_filename_from_text(const char* text,const char* ext,char* out, size_t outcap);
void build_name(char* dst,size_t cap,const char* prefix_s,const char* base_s,const char* index_s,const char* ext_s);
