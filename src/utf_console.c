#include "utf_console.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#include <stdio.h>

void enable_vt_colors(void){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE); if(h==INVALID_HANDLE_VALUE) return;
    DWORD mode=0; if(!GetConsoleMode(h,&mode)) return; mode|=ENABLE_VIRTUAL_TERMINAL_PROCESSING; SetConsoleMode(h,mode);
}

static void w_repeat(wchar_t ch,int count){ for(int i=0;i<count;++i) putwchar(ch); }
static void print_row(const wchar_t* c1,const wchar_t* c2,int col1,int col2){
    if(!c1) c1=L""; if(!c2) c2=L""; wprintf(L"│ %-*.*ls │ %-*.*ls │\n",col1,col1,c1,col2,col2,c2);
}

void print_help(void){
    const int COL1=18,COL2=62; const int B1=COL1+2,B2=COL2+2; int old=_setmode(_fileno(stdout),_O_U16TEXT);
    wprintf(L"\nДоступные команды:\n\n");
    wprintf(L"┌"); w_repeat(L'─',B1); wprintf(L"┬"); w_repeat(L'─',B2); wprintf(L"┐\n");
    print_row(L"Команда",L"Описание",COL1,COL2);
    wprintf(L"├"); w_repeat(L'─',B1); wprintf(L"┼"); w_repeat(L'─',B2); wprintf(L"┤\n");
    print_row(L"help",L"Показать эту справку",COL1,COL2);
    print_row(L"stop",L"Завершить выполнение",COL1,COL2);
    print_row(L"prefix X.Y",L"Установить префикс (A.B)",COL1,COL2);
    print_row(L"prefix off",L"Отключить префикс",COL1,COL2);
    print_row(L"prefix adopt",L"Подхватить следующий B",COL1,COL2);
    print_row(L"package X",L"Установить пакет A=X, B→01",COL1,COL2);
    print_row(L"reindex",L"Нормализовать паддинг %02d.%02d_",COL1,COL2);
    print_row(L"reindex X",L"Перепаковать в пакет A=X",COL1,COL2);
    print_row(L"config reload",L"Перезагрузить конфиг",COL1,COL2);
    print_row(L"clear clipboard",L"Очистить буфер обмена",COL1,COL2);
    print_row(L"status",L"Показать статус",COL1,COL2);
    wprintf(L"└"); w_repeat(L'─',B1); wprintf(L"┴"); w_repeat(L'─',B2); wprintf(L"┘\n\n");
    _setmode(_fileno(stdout),old);
}
