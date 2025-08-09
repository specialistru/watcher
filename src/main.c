// Блок: Точка входа
#include "app_state.h"
#include "app_config.h"
#include "save_clipboard.h"
#include "commands.h"
#include <windows.h>
#include <process.h>
#include <locale.h>
#include <stdio.h>
#include <wchar.h>

int main(void){
    SetConsoleCP(65001); SetConsoleOutputCP(65001); setlocale(LC_ALL, "Russian_Russia.65001");
    app_state_init_defaults();
    AppState* S=app_state();
    load_config(&S->cfg);
    enable_autostart_if_needed(&S->cfg);

    wchar_t* last_text=NULL;
    puts("🚀 Запуск. Введите 'help' для списка команд.\n");

    HANDLE hThread=(HANDLE)_beginthreadex(NULL,0,input_thread_func,NULL,0,NULL);
    while(InterlockedCompareExchange(&S->running,1,1)){
        check_clipboard_and_save(&last_text);
        Sleep(S->cfg.poll_interval_ms);
    }
    free(last_text);
    WaitForSingleObject(hThread,INFINITE); CloseHandle(hThread);
    puts("👋 Завершено.");
    return 0;
}
