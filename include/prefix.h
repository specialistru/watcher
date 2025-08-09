#pragma once
// Блок: Управление префиксом и пакетами
#include <stdbool.h>

bool set_prefix(const char* arg);
bool set_package(const char* arg);
void update_prefix_for_base(const char* base,const char* ext);
int  adopt_next_B_from_fs(void);
