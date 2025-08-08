#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>
#include <process.h>
#include <stdbool.h>
#include <ctype.h>
#include <wchar.h> // нужно для wcscmp, _wcsdup

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

    if (strcmp(ext, "h") == 0)
    {
        const char *ifndef = strstr(text, "#ifndef ");
        const char *define = strstr(text, "#define ");
        if (ifndef && define)
        {
            start = ifndef + 8;
            sscanf(start, "%255s", base);
            strip_common_suffixes(base);
        }
    }
    else if (strcmp(ext, "c") == 0)
    {
        start = strstr(text, "#include \"");
        if (start)
        {
            start += 10;
            sscanf(start, "%[^\".]", base);
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
        // index_suffix = "_%d" (маленький фиксированный буфер — безопасно)
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
    bool has_ifndef = strstr(text, "#ifndef") != NULL;
    bool has_define = strstr(text, "#define") != NULL;
    bool has_endif = strstr(text, "#endif") != NULL;
    bool has_pragma_once = strstr(text, "#pragma once") != NULL;
    bool has_typedef_or_struct = (strstr(text, "typedef ") != NULL) ||
                                 (strstr(text, "struct ") != NULL) ||
                                 (strstr(text, "enum ") != NULL);
    return (has_ifndef && has_define && has_endif) || has_pragma_once || has_typedef_or_struct;
}

bool is_c_file(const char *text)
{
    if (is_h_file(text))
        return false;

    return (strstr(text, "int main(") != NULL) ||
           (strstr(text, "return") != NULL) ||
           (strstr(text, "for (") != NULL) ||
           (strstr(text, "while (") != NULL) ||
           (strstr(text, "#include \"") != NULL);
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

void print_help()
{
    printf("\n📖 Доступные команды:\n\n");
    printf("┌────────────────────┬──────────────────────────────────────────────────────────────┐\n");
    printf("│ %-18s │ %-62s │\n", "Команда", "Описание");
    printf("├────────────────────┼──────────────────────────────────────────────────────────────┤\n");
    printf("│ %-18s │ %-62s │\n", "help", "Показать эту справку");
    printf("│ %-18s │ %-62s │\n", "stop", "Завершить выполнение программы");
    printf("│ %-18s │ %-62s │\n", "prefix X.Y", "Установить префикс для имени файла, например: 1.2_");
    printf("│ %-18s │ %-62s │\n", "prefix off", "Отключить префикс");
    printf("│ %-18s │ %-62s │\n", "config reload", "Перезагрузить конфигурацию из ini-файла");
    printf("│ %-18s │ %-62s │\n", "clear clipboard", "Очистить буфер обмена вручную");
    printf("│ %-18s │ %-62s │\n", "status", "Показать текущий статус (префикс, настройки, состояние)");
    printf("└────────────────────┴──────────────────────────────────────────────────────────────┘\n\n");
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
        snprintf(prefix, MAX_FILENAME, "%d.%d_", a, b);
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
    char buffer[128];
    while (InterlockedCompareExchange(&running, 1, 1))
    {
        if (fgets(buffer, sizeof(buffer), stdin))
        {
            buffer[strcspn(buffer, "\r\n")] = 0;

            if (strcmp(buffer, "stop") == 0)
            {
                InterlockedExchange(&running, 0);
                printf("🛑 Получена команда остановки. Завершаем программу...\n");
                break;
            }
            else if (strcmp(buffer, "help") == 0)
            {
                print_help();
            }
            else if (strncmp(buffer, "prefix ", 7) == 0)
            {
                const char *arg = buffer + 7;
                if (strcmp(arg, "off") == 0)
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
            else if (strcmp(buffer, "config reload") == 0)
            {
                load_config(&config);
                printf("🔄 Конфигурация перезагружена из %s\n", CONFIG_FILE);
            }
            else if (strcmp(buffer, "clear clipboard") == 0)
            {
                clear_clipboard_manual();
            }
            else if (strcmp(buffer, "status") == 0)
            {
                print_status();
            }
            else
            {
                printf("❓ Неизвестная команда. Введите 'help' для списка команд.\n");
            }
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
