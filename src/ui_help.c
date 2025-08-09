#include "ui_help.h"
#include "app_state.h"
#include <windows.h>
#include <stdio.h>

void print_status(void){ AppState* S=app_state(); printf("\n🔎 Статус:\n"); printf("  Префикс: %s\n", S->prefix_enabled? S->prefix : "(отключён)"); printf("  Очистка после сохранения: %s\n", S->cfg.clear_clipboard_after_save?"Вкл":"Выкл"); printf("  Интервал опроса (мс): %d\n", S->cfg.poll_interval_ms); printf("  Автозапуск: %s\n\n", S->cfg.autostart?"Вкл":"Выкл"); }

void clear_clipboard_manual(void){ if(OpenClipboard(NULL)){ EmptyClipboard(); CloseClipboard(); printf("🧹 Буфер обмена очищен вручную.\n"); } else { printf("❌ Не удалось открыть буфер обмена.\n"); } }
