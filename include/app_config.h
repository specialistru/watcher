#pragma once
// Блок: Конфигурация и автозапуск
#include "app_state.h"

#define CONFIG_FILE "clipboard_watcher.ini"

void load_config(AppConfig* cfg);
void enable_autostart_if_needed(const AppConfig* cfg);
