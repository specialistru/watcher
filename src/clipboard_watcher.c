#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>
#include <process.h>
#include <stdbool.h>
#include <ctype.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <conio.h>

#define MAX_FILENAME 256
#define CONFIG_FILE "clipboard_watcher.ini"

volatile LONG running = 1;

typedef struct
{
    int clear_clipboard_after_save;
    int poll_interval_ms;
    int autostart;
} AppConfig;

AppConfig config;

// Глобальные переменные для префикса
char prefix[MAX_FILENAME] = "";
bool prefix_enabled = false;

// --- Auto-increment для prefix A.B_ ---
static int g_prefix_A = 0;
static int g_prefix_B = 0;

// текущая «база» (имя без расширения) и маска увиденных расширений:
// bit0 = видели .h, bit1 = видели .c
static char g_curr_base[MAX_FILENAME] = "";
static unsigned g_curr_mask = 0;

// ===== Безопасные утилиты работы со строками =====
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

// Разрешаем ANSI-последовательности (цвета) в Windows 10+ консоли
static void enable_vt_colors(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode))
        return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(h, mode);
}

// Регистронезависимое startswith
static int istarts_with(const char *s, const char *prefix)
{
    while (*s && *prefix)
    {
        char a = *s, b = *prefix;
        if ('A' <= a && a <= 'Z')
            a += 'a' - 'A';
        if ('A' <= b && b <= 'Z')
            b += 'a' - 'A';
        if (a != b)
            return 0;
        ++s;
        ++prefix;
    }
    return *prefix == '\0';
}

// Очистить хвост строки справа от курсора
static void clear_eol(void)
{
    fputs("\x1b[K", stdout); // ANSI EL - clear to end of line
}

static const char *COMMANDS[] = {
    "help",
    "stop",
    "prefix X.Y",
    "prefix off",
    "config reload",
    "clear clipboard",
    "status",
};
enum
{
    NCOMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0])
};

// Найти подсказку: первая подходящая команда по префиксу (регистр не важен).
// Возвращает полный вариант или NULL.
static const char *find_suggestion(const char *typed)
{
    for (int i = 0; i < NCOMMANDS; ++i)
    {
        if (istarts_with(COMMANDS[i], typed))
            return COMMANDS[i];
    }
    return NULL;
}

// Нарисовать строку ввода с серой подсказкой-«призраком».
// Курсор остаётся сразу после введённого текста.
static void render_prompt(const char *prompt, const char *buf, const char *suggestion)
{
    // Перейти в начало строки и вывести промпт + буфер
    fputs("\r", stdout);
    fputs(prompt, stdout);
    fputs(buf, stdout);
    clear_eol();

    if (suggestion)
    {
        size_t bl = strlen(buf);
        size_t sl = strlen(suggestion);
        if (sl > bl)
        {
            const char *tail = suggestion + bl; // «недописанный хвост»
            fputs("\x1b[90m", stdout);          // серый
            fputs(tail, stdout);
            fputs("\x1b[0m", stdout); // сброс цвета
            // Вернуть курсор назад на длину хвоста
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
        n = cap - 1; // оставляем место под '\0'
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

        // Enter
        if (ch == '\r' || ch == '\n')
        {
            // принять подсказку, если буфер — префикс
            const char *sugg = find_suggestion(buf);
            if (sugg && _stricmp(sugg, buf) != 0 && istarts_with(sugg, buf))
            {
                str_copy_trunc(buf, sizeof(buf), sugg); // вместо strncpy(...)
                len = strlen(buf);
            }
            // вывести финальную строку и перейти вниз
            render_prompt(PROMPT, buf, NULL);
            fputs("\n", stdout);
            str_copy_trunc(out, cap, buf); // вместо strncpy(...)
            return 1;
        }

        // Backspace
        if (ch == 8 /* BS */)
        {
            if (len > 0)
            {
                buf[--len] = '\0';
            }
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }

        // Tab — принять подсказку (если есть)
        if (ch == 9 /* TAB */)
        {
            const char *sugg = find_suggestion(buf);
            if (sugg && _stricmp(sugg, buf) != 0 && istarts_with(sugg, buf))
            {
                str_copy_trunc(buf, sizeof(buf), sugg); // вместо strncpy(...)
                len = strlen(buf);
            }
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }

        // Esc — очистить строку
        if (ch == 27 /* ESC */)
        {
            buf[0] = '\0';
            len = 0;
            render_prompt(PROMPT, buf, find_suggestion(buf));
            continue;
        }

        // Управляющие (стрелки и т.п.)
        if (ch == 0 || ch == 224)
        {
            (void)_getch(); // съесть второй байт
            continue;
        }

        // Печатные символы
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

        // Остальное — игнор
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

// --- Вспомогательные функции ---
int file_exists(const char *filename)
{
    DWORD attr = GetFileAttributesA(filename);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void to_lowercase_and_sanitize(char *str)
{
    for (int i = 0; str[i]; i++)
    {
        str[i] = isalnum((unsigned char)str[i]) || str[i] == '_' ? (char)tolower((unsigned char)str[i]) : '_';
    }
}

void strip_common_suffixes(char *base)
{
    size_t len = strlen(base);
    if (len > 2 && strcmp(&base[len - 2], "_H") == 0)
    {
        base[len - 2] = '\0';
    }
    else if (len > 3 && strcmp(&base[len - 3], "__H") == 0)
    {
        base[len - 3] = '\0';
    }
    else if (len > 8 && strcmp(&base[len - 8], "_INCLUDED") == 0)
    {
        base[len - 8] = '\0';
    }
}

void extract_filename_from_text(const char *text, const char *ext, char *out)
{
    const char *start = NULL;
    char base[MAX_FILENAME] = {0};

    if (_stricmp(ext, "h") == 0)
    {
        const char *ifndef = strstr(text, "#ifndef ");
        const char *define = strstr(text, "#define ");
        if (ifndef && define)
        {
            start = ifndef + 8;
            sscanf(start, "%255s", base);
            strip_common_suffixes(base);
        }
        // если не нашли guard — не гадаем, base останется пустой
    }
    else if (_stricmp(ext, "c") == 0)
    {
        // ищем локальный include "xxx"
        const char *p = text;
        while ((p = strstr(p, "#include")) != NULL)
        {
            const char *q = strchr(p, '\"');
            if (q)
            {
                q++;                          // после первой кавычки
                sscanf(q, "%255[^\"]", base); // до следующей кавычки
                // отрежем .h если есть
                char *dot = strrchr(base, '.');
                if (dot)
                    *dot = '\0';
                break;
            }
            p += 8; // сдвигаем поиск дальше слова include
        }
    }

    if (base[0] == '\0')
    {
        strcpy(base, "output");
    }

    to_lowercase_and_sanitize(base);
    z_buf_copy(out, MAX_FILENAME, base);
}

/* Построение итогового имени файла.
   Собираем: [prefix][base][index_suffix].[ext]
   Всё с учётом cap и оставшегося места, без небезопасных snprintf с %s. */
static void build_name(char *dst, size_t cap,
                       const char *prefix_s,
                       const char *base_s,
                       const char *index_suffix, /* может быть "" */
                       const char *ext_s)
{
    dst[0] = '\0';

    // Добавить префикс (если есть)
    if (prefix_s && prefix_s[0])
    {
        z_buf_cat(dst, cap, prefix_s);
    }

    // Оставшееся место минус: точка + ext
    size_t cur = z_strnlen(dst, cap);
    size_t ext_len = z_strnlen(ext_s ? ext_s : "", MAX_FILENAME);
    size_t min_tail = 1 /* '.' */ + ext_len;
    size_t rem = (cur < cap) ? (cap - 1 - cur) : 0;

    if (rem > min_tail)
    {
        // Влезет base (возможно усечённый) и index_suffix, затем ".ext"
        size_t rem_for_base_and_idx = rem - min_tail;

        // Сначала base
        if (base_s && base_s[0] && rem_for_base_and_idx > 0)
        {
            size_t need_base = z_strnlen(base_s, rem_for_base_and_idx);
            if (need_base)
            {
                char tmp[MAX_FILENAME];
                memcpy(tmp, base_s, need_base);
                tmp[need_base] = '\0';
                z_buf_cat(dst, cap, tmp);
                rem_for_base_and_idx -= need_base;
            }
        }

        // Потом индекс (если есть место)
        if (index_suffix && index_suffix[0] && rem_for_base_and_idx > 0)
        {
            size_t need_idx = z_strnlen(index_suffix, rem_for_base_and_idx);
            if (need_idx)
            {
                char tmp2[MAX_FILENAME];
                memcpy(tmp2, index_suffix, need_idx);
                tmp2[need_idx] = '\0';
                z_buf_cat(dst, cap, tmp2);
                rem_for_base_and_idx -= need_idx;
            }
        }
    }
    // Добавить ".ext"
    z_buf_cat_char(dst, cap, '.');
    z_buf_cat(dst, cap, ext_s ? ext_s : "");
}

// Если base новая (отличается от текущей) — увеличиваем B и обновляем prefix.
// Для .h и .c одной и той же base префикс не меняется.
static void update_prefix_for_base(const char *base, const char *ext)
{
    if (!prefix_enabled || !base || !base[0])
        return;

    if (g_curr_base[0] == '\0')
    {
        // первая пара после установки префикса
        z_buf_copy(g_curr_base, sizeof(g_curr_base), base);
        g_curr_mask = 0;
    }
    else if (strcmp(base, g_curr_base) != 0)
    {
        // новая база → инкремент номера
        g_prefix_B++;
        snprintf(prefix, MAX_FILENAME, "%d.%d_", g_prefix_A, g_prefix_B);
        z_buf_copy(g_curr_base, sizeof(g_curr_base), base);
        g_curr_mask = 0;
    }

    // отмечаем, что для этой базы встретили .h или .c
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
    // Локальные “безопасные” копии
    char prefix_local[MAX_FILENAME];
    prefix_local[0] = '\0';
    char base_local[MAX_FILENAME];
    base_local[0] = '\0';
    char ext_local[MAX_FILENAME];
    ext_local[0] = '\0';

    if (prefix_enabled)
        z_buf_copy(prefix_local, MAX_FILENAME, prefix);
    z_buf_copy(base_local, MAX_FILENAME, base ? base : "file");
    z_buf_copy(ext_local, MAX_FILENAME, ext ? ext : "txt");

    // Сформировать базовое имя
    build_name(out, MAX_FILENAME, prefix_local, base_local, "", ext_local);

    // Если файл существует — добавляем индекс: _0, _1, ...
    int index = 0;
    char idx[32];
    while (file_exists(out))
    {
        snprintf(idx, sizeof(idx), "_%d", index++);
        build_name(out, MAX_FILENAME, prefix_local, base_local, idx, ext_local);
    }

    // Гарантировать NUL
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

    char base[MAX_FILENAME] = {0};
    char filename[MAX_FILENAME] = {0};
    extract_filename_from_text(content_utf8, ext, base);

    // <<< ВАЖНО: обновляем префикс в зависимости от "base" и "ext"
    update_prefix_for_base(base, ext);

    generate_unique_filename(base, ext, filename);

    FILE *file = fopen(filename, "wb");
    if (file)
    {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
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

bool is_h_file(const char *text)
{
    // header = include guard ИЛИ pragma once. Больше ничего не считаем признаком .h
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

    // разумные признаки .c (без «typedef/struct/enum», чтобы не путать)
    if (strstr(text, "#include \"") != NULL)
        return true; // есть локальный инклуд "xxx.h"
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

    const wchar_t *clipboard_text_w = GlobalLock(hData);
    if (!clipboard_text_w)
    {
        CloseClipboard();
        return;
    }

    if (*last_text && wcscmp(*last_text, clipboard_text_w) == 0)
    {
        GlobalUnlock(hData);
        CloseClipboard();
        return;
    }

    free(*last_text);
    *last_text = _wcsdup(clipboard_text_w);

    char content_utf8[65536] = {0};
    WideCharToMultiByte(CP_UTF8, 0, clipboard_text_w, -1, content_utf8, sizeof(content_utf8), NULL, NULL);

    printf("🔍 Найден новый текст в буфере обмена. Анализ...\n");

    if (is_c_file(content_utf8))
    {
        printf("📂 Распознан как: .c файл\n");
        save_to_file(clipboard_text_w, "c");
    }
    else if (is_h_file(content_utf8))
    {
        printf("📂 Распознан как: .h файл\n");
        save_to_file(clipboard_text_w, "h");
    }
    else
    {
        printf("⚠ Не удалось определить тип файла. Сохранение отменено.\n");
    }

    GlobalUnlock(hData);
    CloseClipboard();

    if (config.clear_clipboard_after_save)
    {
        if (OpenClipboard(NULL))
        {
            EmptyClipboard();
            CloseClipboard();
            printf("🧹 Буфер обмена очищен.\n");
        }
    }
}

// --- Конфиг и автозапуск ---

void load_config(AppConfig *config)
{
    config->clear_clipboard_after_save = GetPrivateProfileIntA("General", "clear_clipboard_after_save", 1, CONFIG_FILE);
    config->poll_interval_ms = GetPrivateProfileIntA("General", "poll_interval_ms", 2000, CONFIG_FILE);
    config->autostart = GetPrivateProfileIntA("General", "autostart", 0, CONFIG_FILE);
}

void enable_autostart_if_needed(const AppConfig *config)
{
    if (!config->autostart)
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

// --- Командная строка ---

// Повторить wide-символ count раз (для '─')
static void w_repeat(wchar_t ch, int count)
{
    for (int i = 0; i < count; ++i)
        putwchar(ch);
}

// Печать одной строки таблицы с обрезкой по ширине (в символах)
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
    // Ширины содержимого (в символах, без рамок)
    const int COL1 = 18;     // "Команда"
    const int COL2 = 62;     // "Описание"
    const int B1 = COL1 + 2; // + по одному пробелу слева/справа внутри ячейки
    const int B2 = COL2 + 2;

    // Включаем wide-режим консоли на время печати таблицы
    int old_mode = _setmode(_fileno(stdout), _O_U16TEXT);

    wprintf(L"\nДоступные команды:\n\n");

    // Верхняя граница
    wprintf(L"┌");
    w_repeat(L'─', B1);
    wprintf(L"┬");
    w_repeat(L'─', B2);
    wprintf(L"┐\n");

    // Заголовок
    print_row(L"Команда", L"Описание", COL1, COL2);

    // Разделитель заголовка
    wprintf(L"├");
    w_repeat(L'─', B1);
    wprintf(L"┼");
    w_repeat(L'─', B2);
    wprintf(L"┤\n");

    // Строки
    print_row(L"help", L"Показать эту справку", COL1, COL2);
    print_row(L"stop", L"Завершить выполнение программы", COL1, COL2);
    print_row(L"prefix X.Y", L"Установить префикс для имени файла, например: 1.2_", COL1, COL2);
    print_row(L"prefix off", L"Отключить префикс", COL1, COL2);
    print_row(L"config reload", L"Перезагрузить конфигурацию из ini-файла", COL1, COL2);
    print_row(L"clear clipboard", L"Очистить буфер обмена вручную", COL1, COL2);
    print_row(L"status", L"Показать текущий статус (префикс, настройки, состояние)", COL1, COL2);

    // Нижняя граница
    wprintf(L"└");
    w_repeat(L'─', B1);
    wprintf(L"┴");
    w_repeat(L'─', B2);
    wprintf(L"┘\n\n");

    // Вернём старый режим вывода (обычно _O_TEXT или _O_U8TEXT)
    _setmode(_fileno(stdout), old_mode);
}

void print_status()
{
    printf("\n🔎 Текущий статус:\n");
    printf("  Префикс: %s\n", prefix_enabled ? prefix : "(отключён)");
    printf("  Настройки из конфигурации:\n");
    printf("    Очистка буфера после сохранения: %s\n", config.clear_clipboard_after_save ? "Вкл" : "Выкл");
    printf("    Интервал опроса (мс): %d\n", config.poll_interval_ms);
    printf("    Автозапуск: %s\n\n", config.autostart ? "Вкл" : "Выкл");
}

void clear_clipboard_manual()
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

bool set_prefix(const char *arg)
{
    if (strlen(arg) >= MAX_FILENAME - 2)
    {
        printf("❌ Префикс слишком длинный.\n");
        return false;
    }
    int a, b;
    if (sscanf(arg, "%d.%d", &a, &b) == 2)
    {
        g_prefix_A = a;
        g_prefix_B = b;

        g_curr_base[0] = '\0';
        g_curr_mask = 0;

        snprintf(prefix, MAX_FILENAME, "%d.%d_", g_prefix_A, g_prefix_B);
        prefix_enabled = true;
        printf("⚙️ Префикс установлен: %s\n", prefix);
        return true;
    }
    printf("❌ Некорректный формат префикса. Используйте пример: 1.2\n");
    return false;
}

unsigned __stdcall input_thread_func(void *arg)
{
    (void)arg;

    // Включаем поддержку ANSI-цветов (для серого хвоста) один раз
    enable_vt_colors();

    char line[256];

    for (;;)
    {
        // Если пришла команда остановки из другого потока — выходим
        if (!InterlockedCompareExchange(&running, 1, 1))
            break;

        // Читаем строку с интерактивной подсказкой
        if (!read_command_with_suggestion(line, sizeof(line)))
        {
            // EOF/ошибка чтения — корректно завершаем
            InterlockedExchange(&running, 0);
            break;
        }

        // Трим справа
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
        {
            line[--len] = '\0';
        }

        // Пропускаем ведущие пробелы
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;

        // Пустая строка — продолжаем ожидание
        if (*p == '\0')
            continue;

        // Обработка команд (регистр не важен)
        if (_stricmp(p, "stop") == 0)
        {
            InterlockedExchange(&running, 0);
            printf("🛑 Получена команда остановки. Завершаем программу...\n");
            break;
        }
        else if (_stricmp(p, "help") == 0)
        {
            print_help();
        }
        else if (_strnicmp(p, "prefix ", 7) == 0)
        {
            const char *arg = p + 7;
            while (*arg == ' ' || *arg == '\t')
                ++arg; // допускаем лишние пробелы
            if (_stricmp(arg, "off") == 0)
            {
                prefix_enabled = false;
                prefix[0] = '\0';
                printf("⚙️ Префикс отключён.\n");
            }
            else
            {
                set_prefix(arg);
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
        else
        {
            printf("❓ Неизвестная команда. Введите 'help' для списка команд.\n");
        }
    }

    return 0;
}

// --- Главная функция ---

int main()
{
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    setlocale(LC_ALL, "Russian_Russia.65001");

    load_config(&config);
    enable_autostart_if_needed(&config);

    wchar_t *last_text = NULL;

    printf("🚀 Программа запущена.\nНаберите 'help' для списка команд.\n\n");

    HANDLE hInputThread = (HANDLE)_beginthreadex(NULL, 0, input_thread_func, NULL, 0, NULL);

    while (InterlockedCompareExchange(&running, 1, 1))
    {
        check_clipboard_and_save(&last_text);
        Sleep(config.poll_interval_ms);
    }

    free(last_text);
    WaitForSingleObject(hInputThread, INFINITE);
    CloseHandle(hInputThread);

    printf("👋 Программа завершена.\n");
    return 0;
}
