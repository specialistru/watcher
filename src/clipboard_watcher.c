/************************************************************************************************************************
 * Модуль:       clipboard_watcher.c
 * Назначение:   Отслеживает буфер обмена Windows, определяет тип текста как .h/.c, формирует имя файла и сохраняет.
 * Автор:        anonymous
 * Дата:         2025-08-08
 *
 * Краткое описание:
 *   - Поддержка автодополнения команд (help/stop/prefix/...).
 *   - Нумерация файлов с префиксом A.B_ по «парам» базовых имён (.h и .c) одной логической сущности.
 *   - Безопасные обёртки для работы со строками (обрезка по размеру, защита от переполнений).
 *
 * Примечания по использованию:
 *   - Консоль должна быть в UTF-8: SetConsoleCP(65001)/SetConsoleOutputCP(65001).
 *   - Для ровной рамки таблиц используется wide-режим (_O_U16TEXT) на время печати справки.
 *   - Параметр prefix X.Y включает автонумерацию; prefix off — отключает.
 *
 * Примечания по безопасности:
 *   - Везде соблюдается проверка границ буферов (капасити-ориентированные копии и конкатенации).
 *   - Все системные ресурсы (буфер обмена, файлы) закрываются сразу после использования.
 *   - Нет небезопасных функций наподобие gets/strcpy без контроля размера.
 *
 * Ограничения:
 *   - Определение .c/.h — эвристическое; для некоторых текстов может потребоваться доработка.
 *   - Нумерация префиксов зависит от последовательности поступающих в буфер обмена текстов.
 ************************************************************************************************************************/

#include <windows.h> /* WinAPI: GetClipboardData, GetFileAttributesA, ...                                 */
#include <stdio.h>   /* Стандартный ввод/вывод                                                             */
#include <string.h>  /* Работа со строками                                                                 */
#include <stdlib.h>  /* malloc/free                                                                         */
#include <stdint.h>  /* фиксированные целочисленные типы                                                    */
#include <locale.h>  /* setlocale                                                                           */
#include <process.h> /* _beginthreadex                                                                      */
#include <stdbool.h> /* bool                                                                                */
#include <ctype.h>   /* isalnum/tolower                                                                     */
#include <wchar.h>   /* wide-строки                                                                         */
#include <io.h>      /* _setmode                                                                            */
#include <fcntl.h>   /* _O_U16TEXT                                                                          */
#include <conio.h>   /* _getch                                                                              */

/*---------------------------------------------- DEFINES -----------------------------------------------------------*/
#define MAX_FILENAME 256                    /* Максимальная длина имени файла (включая '\0')                  */
#define CONFIG_FILE "clipboard_watcher.ini" /* Путь к INI-файлу конфигурации                                  */

/* --- forward declarations for reindex helpers --- */
static void plan_or_rename(const char *src, const char *dst, int dry_run);
static void reindex_workspace(int dry_run);        /* pad-only: сохранить A,B, добавить %02d */
static void reindex_repackage(int A, int dry_run); /* перепаковать: A фиксируем, B = 01.. */
static void reindex_workspace_process_one(const char *fname, int dry_run);

/*------------------------------------------- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ -----------------------------------------------*/
volatile LONG running = 1; /* Флаг выполнения цикла, атомарный для потоков                   */

/* Структура параметров приложения. */
typedef struct
{
    int clear_clipboard_after_save; /* 1 — очищать буфер после сохранения; 0 — нет                    */
    int poll_interval_ms;           /* Интервал опроса буфера обмена в миллисекундах                  */
    int autostart;                  /* 1 — включить автозапуск через реестр; 0 — нет                  */
} AppConfig;

AppConfig config; /* Текущая конфигурация                                          */

/* Префикс имён файлов, управляется командами prefix/off. */
char prefix[MAX_FILENAME] = ""; /* Текущий префикс в строковом виде, например "4.5_"              */
bool prefix_enabled = false;    /* Признак включённого префикса                                   */

/* Числовые части префикса A.B_ */
static int g_prefix_A = 0; /* Старшая часть префикса                                         */
static int g_prefix_B = 0; /* Младшая (инкрементируемая) часть                               */

/* Текущая «база» имени (без расширения) и маска встреченных расширений для пары (.h/.c). */
static char g_curr_base[MAX_FILENAME] = ""; /* Имя без расширения для текущей пары                            */
static unsigned g_curr_mask = 0;            /* bit0: .h видели, bit1: .c видели                               */

/*--------------------------------------- УТИЛИТЫ РАБОТЫ СО СТРОКАМИ ----------------------------------------------*/
static size_t z_strnlen(const char *s, size_t max)
{
    size_t i = 0;
    if (!s)
        return 0;
    for (; i < max && s[i]; ++i)
    {
    }
    return i;
}

static void enable_vt_colors(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode))
        return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    (void)SetConsoleMode(h, mode);
}

static int istarts_with(const char *s, const char *prefix)
{
    while (*s && *prefix)
    {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z')
            a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z')
            b += 'a' - 'A';
        if (a != b)
            return 0;
        ++s;
        ++prefix;
    }
    return *prefix == '\0';
}

static void clear_eol(void)
{
    fputs("\x1b[K", stdout);
}

/*--------------------------------------- АВТОПОДСКАЗКА КОМАНД -----------------------------------------------------*/
static const char *COMMANDS[] = {
    "help",
    "stop",
    "prefix X.Y",
    "prefix off",
    "prefix adopt",
    "package X",
    "reindex",
    "reindex X",
    "config reload",
    "clear clipboard",
    "status",
};
enum
{
    NCOMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0])
};

static const char *find_suggestion(const char *typed)
{
    for (int i = 0; i < NCOMMANDS; ++i)
        if (istarts_with(COMMANDS[i], typed))
            return COMMANDS[i];
    return NULL;
}

static void render_prompt(const char *prompt, const char *buf, const char *suggestion)
{
    fputs("\r", stdout);
    fputs(prompt, stdout);
    fputs(buf, stdout);
    clear_eol();
    if (suggestion)
    {
        size_t bl = strlen(buf), sl = strlen(suggestion);
        if (sl > bl)
        {
            const char *tail = suggestion + bl;
            fputs("\x1b[90m", stdout);
            fputs(tail, stdout);
            fputs("\x1b[0m", stdout);
            printf("\x1b[%zuD", strlen(tail));
        }
    }
    fflush(stdout);
}

static void str_copy_trunc(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    if (n)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static int read_command_with_suggestion(char *out, size_t cap)
{
    const char *PROMPT = "> ";
    char buf[256] = {0};
    size_t len = 0;
    render_prompt(PROMPT, buf, find_suggestion(buf));
    for (;;)
    {
        int ch = _getch();
        if (ch == '\r' || ch == '\n')
        {
            const char *s = find_suggestion(buf);
            if (s && _stricmp(s, buf) != 0 && istarts_with(s, buf))
            {
                str_copy_trunc(buf, sizeof(buf), s);
                len = strlen(buf);
            }
            render_prompt(PROMPT, buf, NULL);
            fputs("\n", stdout);
            str_copy_trunc(out, cap, buf);
            return 1;
        }
        if (ch == 8)
        {
            if (len)
                buf[--len] = '\0';
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }
        if (ch == 9)
        {
            const char *s = find_suggestion(buf);
            if (s && _stricmp(s, buf) != 0 && istarts_with(s, buf))
            {
                str_copy_trunc(buf, sizeof(buf), s);
                len = strlen(buf);
            }
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }
        if (ch == 27)
        {
            buf[0] = '\0';
            len = 0;
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }
        if (ch == 0 || ch == 224)
        {
            (void)_getch();
            continue;
        }
        if (ch >= 32 && ch < 127)
        {
            if (len + 1 < sizeof(buf))
            {
                buf[len++] = (char)ch;
                buf[len] = '\0';
            }
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }
    }
}

static void z_buf_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    size_t n = (cap > 0) ? z_strnlen(src, cap - 1) : 0;
    if (n)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static void z_buf_cat(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    size_t cur = z_strnlen(dst, cap);
    if (cur >= cap - 1)
        return;
    size_t rem = cap - 1 - cur;
    size_t n = z_strnlen(src ? src : "", rem);
    if (n)
        memcpy(dst + cur, src, n);
    dst[cur + n] = '\0';
}

static void z_buf_cat_char(char *dst, size_t cap, char ch)
{
    if (!dst || cap == 0)
        return;
    size_t cur = z_strnlen(dst, cap);
    if (cur >= cap - 1)
        return;
    dst[cur] = ch;
    dst[cur + 1] = '\0';
}

/*--------------------------------------- ФАЙЛОВЫЕ И ВСПОМОГАТЕЛЬНЫЕ ----------------------------------------------*/
int file_exists(const char *filename)
{
    DWORD attr = GetFileAttributesA(filename);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void to_lowercase_and_sanitize(char *str)
{
    for (int i = 0; str[i]; i++)
        str[i] = (isalnum((unsigned char)str[i]) || str[i] == '_') ? (char)tolower((unsigned char)str[i]) : '_';
}

void strip_common_suffixes(char *base)
{
    size_t len = strlen(base);
    if (len > 2 && strcmp(&base[len - 2], "_H") == 0)
        base[len - 2] = '\0';
    else if (len > 3 && strcmp(&base[len - 3], "__H") == 0)
        base[len - 3] = '\0';
    else if (len > 8 && strcmp(&base[len - 8], "_INCLUDED") == 0)
        base[len - 8] = '\0';
}

void extract_filename_from_text(const char *text, const char *ext, char *out)
{
    char base[MAX_FILENAME] = {0};
    if (_stricmp(ext, "h") == 0)
    {
        const char *ifndef = strstr(text, "#ifndef ");
        const char *define = strstr(text, "#define ");
        if (ifndef && define)
        {
            const char *start = ifndef + 8;
            sscanf(start, "%255s", base);
            strip_common_suffixes(base);
        }
    }
    else if (_stricmp(ext, "c") == 0)
    {
        const char *p = text;
        while ((p = strstr(p, "#include")) != NULL)
        {
            const char *q1 = strchr(p, '\"');
            if (!q1)
            {
                p += 8;
                continue;
            }
            q1++;
            const char *q2 = strchr(q1, '\"');
            if (!q2)
            {
                p += 8;
                continue;
            }
            size_t n = (size_t)(q2 - q1);
            if (n > 0 && n < sizeof(base))
            {
                memcpy(base, q1, n);
                base[n] = '\0';
                char *dot = strrchr(base, '.');
                if (dot)
                    *dot = '\0';
                break;
            }
            p = q2 + 1;
        }
    }
    if (base[0] == '\0')
        strcpy(base, "output");
    to_lowercase_and_sanitize(base);
    z_buf_copy(out, MAX_FILENAME, base);
}

static void build_name(char *dst, size_t cap, const char *prefix_s, const char *base_s,
                       const char *index_suffix, const char *ext_s)
{
    dst[0] = '\0';
    if (prefix_s && prefix_s[0])
        z_buf_cat(dst, cap, prefix_s);

    size_t cur = z_strnlen(dst, cap);
    size_t ext_len = z_strnlen(ext_s ? ext_s : "", MAX_FILENAME);
    size_t min_tail = 1 + ext_len;
    size_t rem = (cur < cap) ? (cap - 1 - cur) : 0;

    if (rem > min_tail)
    {
        size_t room = rem - min_tail;
        if (base_s && base_s[0] && room)
        {
            size_t nb = z_strnlen(base_s, room);
            if (nb)
            {
                char tmp[MAX_FILENAME];
                memcpy(tmp, base_s, nb);
                tmp[nb] = '\0';
                z_buf_cat(dst, cap, tmp);
                room -= nb;
            }
        }
        if (index_suffix && index_suffix[0] && room)
        {
            size_t ni = z_strnlen(index_suffix, room);
            if (ni)
            {
                char tmp2[MAX_FILENAME];
                memcpy(tmp2, index_suffix, ni);
                tmp2[ni] = '\0';
                z_buf_cat(dst, cap, tmp2);
            }
        }
    }
    z_buf_cat_char(dst, cap, '.');
    z_buf_cat(dst, cap, ext_s ? ext_s : "");
}

/* set_package: A=X, B=01 */
static bool set_package(const char *arg)
{
    int a;
    if (!arg || sscanf(arg, "%d", &a) != 1 || a < 0)
    {
        puts("❌ Некорректный номер пакета. Пример: package 5");
        return false;
    }
    g_prefix_A = a;
    g_prefix_B = 1;
    g_curr_base[0] = '\0';
    g_curr_mask = 0;
    _snprintf(prefix, MAX_FILENAME, "%02d.%02d_", g_prefix_A, g_prefix_B);
    prefix_enabled = true;
    printf("📦 Пакет установлен: A=%02d, B=%02d → префикс %s\n", g_prefix_A, g_prefix_B, prefix);
    return true;
}

static void update_prefix_for_base(const char *base, const char *ext)
{
    if (!prefix_enabled || !base || !base[0])
        return;

    if (g_curr_base[0] == '\0')
    {
        z_buf_copy(g_curr_base, sizeof(g_curr_base), base);
        g_curr_mask = 0;
    }
    else if (strcmp(base, g_curr_base) != 0)
    {
        ++g_prefix_B;
        _snprintf(prefix, MAX_FILENAME, "%02d.%02d_", g_prefix_A, g_prefix_B);
        z_buf_copy(g_curr_base, sizeof(g_curr_base), base);
        g_curr_mask = 0;
    }
    if (ext && ext[0])
    {
        if (ext[0] == 'h' || ext[0] == 'H')
            g_curr_mask |= 1u;
        if (ext[0] == 'c' || ext[0] == 'C')
            g_curr_mask |= 2u;
    }
}

void generate_unique_filename(const char *base, const char *ext, char *out)
{
    char prefix_local[MAX_FILENAME] = "", base_local[MAX_FILENAME] = "", ext_local[MAX_FILENAME] = "";
    if (prefix_enabled)
        z_buf_copy(prefix_local, MAX_FILENAME, prefix);
    z_buf_copy(base_local, MAX_FILENAME, base ? base : "file");
    z_buf_copy(ext_local, MAX_FILENAME, ext ? ext : "txt");

    build_name(out, MAX_FILENAME, prefix_local, base_local, "", ext_local);

    int index = 0;
    char idx[32];
    while (file_exists(out))
    {
        _snprintf(idx, sizeof(idx), "_%d", index++);
        build_name(out, MAX_FILENAME, prefix_local, base_local, idx, ext_local);
    }
    out[MAX_FILENAME - 1] = '\0';
}

void save_to_file(const wchar_t *content_w, const char *ext)
{
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, content_w, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0)
        return;

    char *content_utf8 = (char *)malloc(utf8_len);
    if (!content_utf8)
        return;
    WideCharToMultiByte(CP_UTF8, 0, content_w, -1, content_utf8, utf8_len, NULL, NULL);

    char base[MAX_FILENAME] = {0}, filename[MAX_FILENAME] = {0};
    extract_filename_from_text(content_utf8, ext, base);
    update_prefix_for_base(base, ext);
    generate_unique_filename(base, ext, filename);

    FILE *file = fopen(filename, "wb");
    if (file)
    {
        static const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        fwrite(bom, sizeof(bom), 1, file);
        fputs(content_utf8, file);
        fclose(file);
        printf("✅ Сохранено: %s\n", filename);
    }
    else
    {
        printf("❌ Ошибка при сохранении файла.\n");
    }
    free(content_utf8);
}

/*--------------------------------------- ОПРЕДЕЛЕНИЕ ТИПА ТЕКСТА --------------------------------------------------*/
bool is_h_file(const char *text)
{
    const char *ifndef_ = strstr(text, "#ifndef");
    const char *define_ = strstr(text, "#define");
    const char *endif_ = strstr(text, "#endif");
    bool has_guards = (ifndef_ && define_ && endif_);
    bool has_pragma_once = (strstr(text, "#pragma once") != NULL);
    return has_guards || has_pragma_once;
}

bool is_c_file(const char *text)
{
    if (is_h_file(text))
        return false;
    if (strstr(text, "#include \"") != NULL)
        return true;
    if (strstr(text, "int main(") != NULL)
        return true;
    if (strstr(text, "for (") != NULL)
        return true;
    if (strstr(text, "while (") != NULL)
        return true;
    if (strstr(text, "return") != NULL)
        return true;
    return false;
}

/*--------------------------------------- ОСНОВНАЯ ЛОГИКА СЧИТЫВАНИЯ -----------------------------------------------*/
void check_clipboard_and_save(wchar_t **last_text)
{
    if (!OpenClipboard(NULL))
        return;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData)
    {
        CloseClipboard();
        return;
    }

    const wchar_t *w = GlobalLock(hData);
    if (!w)
    {
        CloseClipboard();
        return;
    }

    if (*last_text && wcscmp(*last_text, w) == 0)
    {
        GlobalUnlock(hData);
        CloseClipboard();
        return;
    }

    free(*last_text);
    *last_text = _wcsdup(w);

    char u8[65536] = {0};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, sizeof(u8), NULL, NULL);
    printf("🔍 Найден новый текст в буфере обмена. Анализ...\n");

    if (is_c_file(u8))
    {
        printf("📂 Распознан как: .c файл\n");
        save_to_file(w, "c");
    }
    else if (is_h_file(u8))
    {
        printf("📂 Распознан как: .h файл\n");
        save_to_file(w, "h");
    }
    else
    {
        printf("⚠ Не удалось определить тип файла. Сохранение отменено.\n");
    }

    GlobalUnlock(hData);
    CloseClipboard();

    if (config.clear_clipboard_after_save)
        if (OpenClipboard(NULL))
        {
            EmptyClipboard();
            CloseClipboard();
            printf("🧹 Буфер обмена очищен.\n");
        }
}

/*--------------------------------------- КОНФИГ/АВТОЗАПУСК --------------------------------------------------------*/
void load_config(AppConfig *cfg)
{
    cfg->clear_clipboard_after_save = GetPrivateProfileIntA("General", "clear_clipboard_after_save", 1, CONFIG_FILE);
    cfg->poll_interval_ms = GetPrivateProfileIntA("General", "poll_interval_ms", 2000, CONFIG_FILE);
    cfg->autostart = GetPrivateProfileIntA("General", "autostart", 0, CONFIG_FILE);
}

void enable_autostart_if_needed(const AppConfig *cfg)
{
    if (!cfg->autostart)
        return;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, sizeof(path)))
    {
        printf("⚠ Не удалось получить путь к EXE.\n");
        return;
    }

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegSetValueExA(hKey, "ClipboardWatcher", 0, REG_SZ, (const BYTE *)path, (DWORD)(strlen(path) + 1));
        RegCloseKey(hKey);
        printf("🛠️ Автозапуск включён через реестр.\n");
    }
    else
    {
        printf("❌ Не удалось установить автозапуск через реестр.\n");
    }
}

/*--------------------------------------- ПЕЧАТЬ СПРАВКИ -----------------------------------------------------------*/
static void w_repeat(wchar_t ch, int count)
{
    for (int i = 0; i < count; ++i)
        putwchar(ch);
}

static void print_row(const wchar_t *c1, const wchar_t *c2, int col1, int col2)
{
    if (!c1)
        c1 = L"";
    if (!c2)
        c2 = L"";
    wprintf(L"│ %-*.*ls │ %-*.*ls │\n", col1, col1, c1, col2, col2, c2);
}

void print_help(void)
{
    const int COL1 = 18, COL2 = 62;
    const int B1 = COL1 + 2, B2 = COL2 + 2;
    int old_mode = _setmode(_fileno(stdout), _O_U16TEXT);

    wprintf(L"\nДоступные команды:\n\n");
    wprintf(L"┌");
    w_repeat(L'─', B1);
    wprintf(L"┬");
    w_repeat(L'─', B2);
    wprintf(L"┐\n");
    print_row(L"Команда", L"Описание", COL1, COL2);
    wprintf(L"├");
    w_repeat(L'─', B1);
    wprintf(L"┼");
    w_repeat(L'─', B2);
    wprintf(L"┤\n");
    print_row(L"help", L"Показать эту справку", COL1, COL2);
    print_row(L"stop", L"Завершить выполнение программы", COL1, COL2);
    print_row(L"prefix X.Y", L"Установить префикс (A.B), включает автонумерацию", COL1, COL2);
    print_row(L"prefix off", L"Отключить префикс", COL1, COL2);
    print_row(L"prefix adopt", L"Подхватить следующий B по существующим файлам", COL1, COL2);
    print_row(L"package X", L"Установить пакет A=X и сбросить B→01", COL1, COL2);
    print_row(L"reindex", L"Нормализовать имена: сохранить A,B; паддинг → %02d.%02d_", COL1, COL2);
    print_row(L"reindex X", L"Перепаковать в пакет A=X, пронумеровать B→01…", COL1, COL2);
    print_row(L"config reload", L"Перезагрузить конфигурацию из ini-файла", COL1, COL2);
    print_row(L"clear clipboard", L"Очистить буфер обмена вручную", COL1, COL2);
    print_row(L"status", L"Показать текущий статус (префикс, настройки, состояние)", COL1, COL2);
    wprintf(L"└");
    w_repeat(L'─', B1);
    wprintf(L"┴");
    w_repeat(L'─', B2);
    wprintf(L"┘\n\n");

    _setmode(_fileno(stdout), old_mode);
}

void print_status(void)
{
    printf("\n🔎 Текущий статус:\n");
    printf("  Префикс: %s\n", prefix_enabled ? prefix : "(отключён)");
    printf("  Настройки из конфигурации:\n");
    printf("    Очистка буфера после сохранения: %s\n", config.clear_clipboard_after_save ? "Вкл" : "Выкл");
    printf("    Интервал опроса (мс): %d\n", config.poll_interval_ms);
    printf("    Автозапуск: %s\n\n", config.autostart ? "Вкл" : "Выкл");
}

void clear_clipboard_manual(void)
{
    if (OpenClipboard(NULL))
    {
        EmptyClipboard();
        CloseClipboard();
        printf("🧹 Буфер обмена очищен вручную.\n");
    }
    else
    {
        printf("❌ Не удалось открыть буфер обмена для очистки.\n");
    }
}

/* AA берём из g_prefix_A; ищем макс. BB среди файлов вида "AA.BB_*.(h|c)" */
static int adopt_next_B_from_fs(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*.*", &fd);
    int maxB = 0;

    if (h == INVALID_HANDLE_VALUE)
        return 1;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        const char *ext = strrchr(fd.cFileName, '.');
        if (!ext || (_stricmp(ext, ".h") && _stricmp(ext, ".c")))
            continue;

        int A = 0, B = 0;
        if (sscanf(fd.cFileName, "%d.%d_", &A, &B) == 2 && A == g_prefix_A)
            if (B > maxB)
                maxB = B;
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return maxB + 1;
}

/* ======================= Реиндексация существующих файлов ======================= */
typedef struct
{
    char base[MAX_FILENAME];
    char h[MAX_FILENAME];
    char c[MAX_FILENAME];
} group_t;

/* Разобрать "A.B_rest.ext": возвращает 1 при успехе; rest без расширения */
static int parse_ab_rest_from_filename(const char *fname, int *A, int *B,
                                       char *rest, size_t restcap)
{
    char stem[MAX_FILENAME] = {0};
    const char *dot = strrchr(fname, '.');
    size_t L = dot ? (size_t)(dot - fname) : strlen(fname);
    if (L >= sizeof(stem))
        L = sizeof(stem) - 1;
    memcpy(stem, fname, L);
    stem[L] = '\0';

    if (sscanf(stem, "%d.%d_%255s", A, B, rest) == 3 && *A >= 0 && *B >= 0)
    {
        rest[restcap - 1] = '\0';
        return 1;
    }
    return 0;
}

static void parse_base_for_repack(const char *fname, char *out)
{
    char rest[MAX_FILENAME] = {0};
    int A, B;
    if (parse_ab_rest_from_filename(fname, &A, &B, rest, sizeof(rest)))
        z_buf_copy(out, MAX_FILENAME, rest);
    else
    {
        char stem[MAX_FILENAME] = {0};
        const char *dot = strrchr(fname, '.');
        size_t L = dot ? (size_t)(dot - fname) : strlen(fname);
        if (L >= sizeof(stem))
            L = sizeof(stem) - 1;
        memcpy(stem, fname, L);
        stem[L] = '\0';
        z_buf_copy(out, MAX_FILENAME, stem);
    }
    to_lowercase_and_sanitize(out);
}

static int cmp_groups_by_base(const void *a, const void *b)
{
    const group_t *ga = (const group_t *)a, *gb = (const group_t *)b;
    return strcmp(ga->base, gb->base);
}

/* reindex_repackage: Назначает всем A, а B считает 01.. по base */
static void reindex_repackage(int A, int dry_run)
{
    group_t g[2048];
    int n = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h;

    memset(g, 0, sizeof(g));

#define ADD_FILE(is_h)                                        \
    do                                                        \
    {                                                         \
        char base[MAX_FILENAME];                              \
        int i;                                                \
        parse_base_for_repack(fd.cFileName, base);            \
        for (i = 0; i < n; ++i)                               \
            if (!strcmp(g[i].base, base))                     \
                break;                                        \
        if (i == n)                                           \
        {                                                     \
            if (n >= (int)(sizeof(g) / sizeof(g[0])))         \
            {                                                 \
                puts("❌ Слишком много групп");               \
                break;                                        \
            }                                                 \
            z_buf_copy(g[n].base, sizeof(g[n].base), base);   \
            g[n].h[0] = g[n].c[0] = '\0';                     \
            ++n;                                              \
        }                                                     \
        if (is_h)                                             \
            z_buf_copy(g[i].h, sizeof(g[i].h), fd.cFileName); \
        else                                                  \
            z_buf_copy(g[i].c, sizeof(g[i].c), fd.cFileName); \
    } while (0)

    h = FindFirstFileA("*.h", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            ADD_FILE(1);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    h = FindFirstFileA("*.c", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            ADD_FILE(0);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    qsort(g, n, sizeof(g[0]), cmp_groups_by_base);

    for (int i = 0, BB = 1; i < n; ++i, ++BB)
    {
        char pref[16];
        _snprintf(pref, sizeof(pref), "%02d.%02d_", A, BB);
        if (g[i].h[0])
        {
            char dst[MAX_FILENAME];
            _snprintf(dst, sizeof(dst), "%s%s.h", pref, g[i].base);
            plan_or_rename(g[i].h, dst, dry_run);
        }
        if (g[i].c[0])
        {
            char dst[MAX_FILENAME];
            _snprintf(dst, sizeof(dst), "%s%s.c", pref, g[i].base);
            plan_or_rename(g[i].c, dst, dry_run);
        }
    }
}

/* dry-run / rename */
static void plan_or_rename(const char *src, const char *dst, int dry_run)
{
    if (strcmp(src, dst) == 0)
    {
        printf("= %s (без изменений)\n", src);
        return;
    }
    if (dry_run)
    {
        printf("→ %s  ==>  %s\n", src, dst);
        return;
    }
    if (file_exists(dst))
    {
        printf("❗ Цель уже существует, пропуск: %s\n", dst);
        return;
    }
    if (MoveFileA(src, dst))
    {
        printf("✔ %s  ->  %s\n", src, dst);
    }
    else
    {
        printf("❌ Переименование не удалось: %s -> %s (код %lu)\n",
               src, dst, (unsigned long)GetLastError());
    }
}

/* --- pad-only reindex: сохранить A,B, добавить ведущие нули --- */
static void reindex_workspace_process_one(const char *fname, int dry_run)
{
    int A = -1, B = -1;
    char rest[MAX_FILENAME] = {0};
    char stem[MAX_FILENAME] = {0};
    const char *dot = strrchr(fname, '.');
    size_t L = dot ? (size_t)(dot - fname) : strlen(fname);
    if (L >= sizeof(stem))
        L = sizeof(stem) - 1;
    memcpy(stem, fname, L);
    stem[L] = '\0';

    if (sscanf(stem, "%d.%d_%255s", &A, &B, rest) == 3 && A >= 0 && B >= 0 && rest[0])
    {
        char dst[MAX_FILENAME];
        _snprintf(dst, sizeof(dst), "%02d.%02d_%s%s",
                  A, B, rest, dot ? dot : "");
        plan_or_rename(fname, dst, dry_run);
    }
    else
    {
        /* пропуск не соответствующих шаблону */
    }
}

static void reindex_workspace(int dry_run)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;

    h = FindFirstFileA("*.h", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            reindex_workspace_process_one(fd.cFileName, dry_run);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    h = FindFirstFileA("*.c", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            reindex_workspace_process_one(fd.cFileName, dry_run);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

/*--------------------------------------- ПРЕФИКС -----------------------------------------------------------*/
bool set_prefix(const char *arg)
{
    if (strlen(arg) >= MAX_FILENAME - 2)
    {
        puts("❌ Префикс слишком длинный.");
        return false;
    }
    int a, b;
    if (sscanf(arg, "%d.%d", &a, &b) != 2)
    {
        puts("❌ Некорректный формат. Пример: 1.2");
        return false;
    }
    g_prefix_A = a;
    g_prefix_B = b;
    g_curr_base[0] = '\0';
    g_curr_mask = 0;
    _snprintf(prefix, MAX_FILENAME, "%02d.%02d_", g_prefix_A, g_prefix_B);
    prefix_enabled = true;
    printf("⚙️ Префикс установлен: %s\n", prefix);
    return true;
}

/*--------------------------------------- ВВОД КОМАНД -----------------------------------------------------------*/
unsigned __stdcall input_thread_func(void *arg)
{
    (void)arg;
    enable_vt_colors();
    char line[256];

    for (;;)
    {
        if (!InterlockedCompareExchange(&running, 1, 1))
            break;
        if (!read_command_with_suggestion(line, sizeof(line)))
        {
            InterlockedExchange(&running, 0);
            break;
        }

        size_t len = strlen(line);
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p)
            continue;

        if (_stricmp(p, "stop") == 0)
        {
            InterlockedExchange(&running, 0);
            puts("🛑 Получена команда остановки. Завершаем программу...");
            break;
        }
        else if (_stricmp(p, "help") == 0)
        {
            print_help();
        }
        else if (_strnicmp(p, "reindex", 7) == 0)
        {
            const char *q = p + 7;
            while (*q == ' ' || *q == '\t')
                ++q;

            int dry = (strstr(q, "--dry-run") != NULL);

            if (*q == '\0' || *q == '-')
            {
                /* reindex [--dry-run] => нормализация паддинга */
                reindex_workspace(dry);
            }
            else
            {
                int A = -1;
                if (sscanf(q, "%d", &A) == 1 && A >= 0)
                {
                    /* reindex A [--dry-run] => перепаковка */
                    reindex_repackage(A, dry);
                }
                else
                {
                    puts("Использование:\n  reindex [--dry-run]\n  reindex A [--dry-run]");
                }
            }
        }
        else if (_stricmp(p, "config reload") == 0)
        {
            load_config(&config);
            printf("🔄 Конфигурация перезагружена из %s\n", CONFIG_FILE);
        }
        else if (_stricmp(p, "clear clipboard") == 0)
        {
            clear_clipboard_manual();
        }
        else if (_stricmp(p, "status") == 0)
        {
            print_status();
        }
        else if (_strnicmp(p, "package ", 8) == 0)
        {
            const char *arg = p + 8;
            while (*arg == ' ' || *arg == '\t')
                ++arg;
            set_package(arg);
        }
        else if (_stricmp(p, "prefix adopt") == 0)
        {
            if (!prefix_enabled)
            {
                puts("Сначала задайте prefix A.B");
                continue;
            }
            g_prefix_B = adopt_next_B_from_fs();
            _snprintf(prefix, MAX_FILENAME, "%02d.%02d_", g_prefix_A, g_prefix_B);
            printf("🔢 Приняли нумерацию из файлов: следующий B=%02d → %s\n", g_prefix_B, prefix);
        }
        else if (_strnicmp(p, "prefix ", 7) == 0)
        {
            const char *a = p + 7;
            while (*a == ' ' || *a == '\t')
                ++a;
            if (_stricmp(a, "off") == 0)
            {
                prefix_enabled = false;
                prefix[0] = '\0';
                puts("⚙️ Префикс отключён.");
            }
            else
            {
                set_prefix(a);
            }
        }
        else
        {
            puts("❓ Неизвестная команда. Введите 'help' для списка команд.");
        }
    }
    return 0;
}

/*------------------------------------------------- MAIN -----------------------------------------------------------*/
int main(void)
{
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    setlocale(LC_ALL, "Russian_Russia.65001");

    load_config(&config);
    enable_autostart_if_needed(&config);

    wchar_t *last_text = NULL;
    puts("🚀 Программа запущена.\nНаберите 'help' для списка команд.\n");

    HANDLE hInputThread = (HANDLE)_beginthreadex(NULL, 0, input_thread_func, NULL, 0, NULL);

    while (InterlockedCompareExchange(&running, 1, 1))
    {
        check_clipboard_and_save(&last_text);
        Sleep(config.poll_interval_ms);
    }

    free(last_text);
    WaitForSingleObject(hInputThread, INFINITE);
    CloseHandle(hInputThread);
    puts("👋 Программа завершена.");
    return 0;
}
