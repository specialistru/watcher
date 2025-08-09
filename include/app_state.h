#pragma once
// Блок: Глобальное состояние приложения (AppState)
#include <windows.h>
#include <stdbool.h>

#define MAX_FILENAME 256

typedef struct {
    int clear_clipboard_after_save;
    int poll_interval_ms;
    int autostart;
} AppConfig;

typedef struct {
    volatile LONG running;           // 1 — работать, 0 — стоп
    AppConfig cfg;                   // конфигурация
    bool prefix_enabled;             // включён ли префикс
    char prefix[MAX_FILENAME];       // строковый префикс "AA.BB_"
    int  prefix_A;                   // AA
    int  prefix_B;                   // BB
    char curr_base[MAX_FILENAME];    // база имени текущей пары
    unsigned curr_mask;              // bit0: .h, bit1: .c
} AppState;

AppState* app_state(void);          // доступ к синглтону
void app_state_init_defaults(void); // инициализация по умолчанию
