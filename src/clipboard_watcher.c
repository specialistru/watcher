#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>
#include <process.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_FILENAME 256
#define CONFIG_FILE "clipboard_watcher.ini"

volatile LONG running = 1;

typedef struct {
    int clear_clipboard_after_save;
    int poll_interval_ms;
    int autostart;
} AppConfig;

AppConfig config;

// Глобальные переменные для префикса
char prefix[MAX_FILENAME] = "";
bool prefix_enabled = false;

// --- Вспомогательные функции ---

int file_exists(const char *filename) {
    DWORD attr = GetFileAttributesA(filename);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void to_lowercase_and_sanitize(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = isalnum((unsigned char)str[i]) || str[i] == '_' ?
                 (char)tolower((unsigned char)str[i]) : '_';
    }
}

void strip_common_suffixes(char *base) {
    size_t len = strlen(base);
    if (len > 2 && strcmp(&base[len - 2], "_H") == 0) {
        base[len - 2] = '\0';
    } else if (len > 3 && strcmp(&base[len - 3], "__H") == 0) {
        base[len - 3] = '\0';
    } else if (len > 8 && strcmp(&base[len - 8], "_INCLUDED") == 0) {
        base[len - 8] = '\0';
    }
}

void extract_filename_from_text(const char *text, const char *ext, char *out) {
    const char *start = NULL;
    char base[MAX_FILENAME] = {0};

    if (strcmp(ext, "h") == 0) {
        const char *ifndef = strstr(text, "#ifndef ");
        const char *define = strstr(text, "#define ");
        if (ifndef && define) {
            start = ifndef + 8;
            sscanf(start, "%255s", base);
            strip_common_suffixes(base);
        }
    } else if (strcmp(ext, "c") == 0) {
        start = strstr(text, "#include \"");
        if (start) {
            start += 10;
            sscanf(start, "%[^\".]", base);
        }
    }

    if (base[0] == '\0') {
        strcpy(base, "output");
    }

    to_lowercase_and_sanitize(base);
    snprintf(out, MAX_FILENAME, "%s", base);
}

void generate_unique_filename(const char *base, const char *ext, char *out)
{
    int index = 0;
    size_t prefix_len = prefix_enabled ? strlen(prefix) : 0;
    size_t base_len = strlen(base);
    size_t ext_len = strlen(ext);

    char index_str[12] = {0};

    if (prefix_enabled)
    {
        if (prefix_len + base_len + 1 + ext_len > MAX_FILENAME - 1)
        {
            size_t allowed_base_len = MAX_FILENAME - 1 - prefix_len - 1 - ext_len;
            if (allowed_base_len > 0 && allowed_base_len < base_len)
            {
                char base_trunc[MAX_FILENAME];
                strncpy(base_trunc, base, allowed_base_len);
                base_trunc[allowed_base_len] = '\0';
                snprintf(out, MAX_FILENAME, "%s%s.%s", prefix, base_trunc, ext);
            }
            else
            {
                snprintf(out, MAX_FILENAME, "%s.%s", prefix, ext);
            }
        }
        else
        {
            snprintf(out, MAX_FILENAME, "%s%s.%s", prefix, base, ext);
        }
    }
    else
    {
        if (base_len + 1 + ext_len > MAX_FILENAME - 1)
        {
            size_t allowed_base_len = MAX_FILENAME - 1 - 1 - ext_len;
            if (allowed_base_len > 0 && allowed_base_len < base_len)
            {
                char base_trunc[MAX_FILENAME];
                strncpy(base_trunc, base, allowed_base_len);
                base_trunc[allowed_base_len] = '\0';
                snprintf(out, MAX_FILENAME, "%s.%s", base_trunc, ext);
            }
            else
            {
                snprintf(out, MAX_FILENAME, "file.%s", ext);
            }
        }
        else
        {
            snprintf(out, MAX_FILENAME, "%s.%s", base, ext);
        }
    }

    while (file_exists(out))
    {
        snprintf(index_str, sizeof(index_str), "_%d", index++);
        // удаляем size_t out_len = 0;

        if (prefix_enabled)
        {
            size_t len = prefix_len + base_len + strlen(index_str) + 1 + ext_len;
            if (len > MAX_FILENAME - 1)
            {
                size_t allowed_base_len = MAX_FILENAME - 1 - prefix_len - strlen(index_str) - 1 - ext_len;
                if (allowed_base_len > 0 && allowed_base_len < base_len)
                {
                    char base_trunc[MAX_FILENAME];
                    strncpy(base_trunc, base, allowed_base_len);
                    base_trunc[allowed_base_len] = '\0';
                    snprintf(out, MAX_FILENAME, "%s%s%s.%s", prefix, base_trunc, index_str, ext);
                }
                else
                {
                    snprintf(out, MAX_FILENAME, "%s%s.%s", prefix, index_str, ext);
                }
            }
            else
            {
                snprintf(out, MAX_FILENAME, "%s%s%s.%s", prefix, base, index_str, ext);
            }
        }
        else
        {
            size_t len = base_len + strlen(index_str) + 1 + ext_len;
            if (len > MAX_FILENAME - 1)
            {
                size_t allowed_base_len = MAX_FILENAME - 1 - strlen(index_str) - 1 - ext_len;
                if (allowed_base_len > 0 && allowed_base_len < base_len)
                {
                    char base_trunc[MAX_FILENAME];
                    strncpy(base_trunc, base, allowed_base_len);
                    base_trunc[allowed_base_len] = '\0';
                    snprintf(out, MAX_FILENAME, "%s%s.%s", base_trunc, index_str, ext);
                }
                else
                {
                    snprintf(out, MAX_FILENAME, "file%s.%s", index_str, ext);
                }
            }
            else
            {
                snprintf(out, MAX_FILENAME, "%s%s.%s", base, index_str, ext);
            }
        }
    }
}

void save_to_file(const wchar_t *content_w, const char *ext) {
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, content_w, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) return;

    char *content_utf8 = malloc(utf8_len);
    if (!content_utf8) return;

    WideCharToMultiByte(CP_UTF8, 0, content_w, -1, content_utf8, utf8_len, NULL, NULL);

    char base[MAX_FILENAME] = {0};
    char filename[MAX_FILENAME] = {0};
    extract_filename_from_text(content_utf8, ext, base);
    generate_unique_filename(base, ext, filename);

    FILE *file = fopen(filename, "wb");
    if (file) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        fwrite(bom, sizeof(bom), 1, file);
        fputs(content_utf8, file);
        fclose(file);
        printf("✅ Сохранено: %s\n", filename);
    } else {
        printf("❌ Ошибка при сохранении файла.\n");
    }

    free(content_utf8);
}

bool is_h_file(const char *text) {
    bool has_ifndef = strstr(text, "#ifndef") != NULL;
    bool has_define = strstr(text, "#define") != NULL;
    bool has_endif = strstr(text, "#endif") != NULL;
    bool has_pragma_once = strstr(text, "#pragma once") != NULL;
    bool has_typedef_or_struct = strstr(text, "typedef ") || strstr(text, "struct ") || strstr(text, "enum ");
    return (has_ifndef && has_define && has_endif) || has_pragma_once || has_typedef_or_struct;
}

bool is_c_file(const char *text) {
    if (is_h_file(text)) return false;

    return strstr(text, "int main(") ||
           strstr(text, "return") ||
           strstr(text, "for (") ||
           strstr(text, "while (") ||
           strstr(text, "#include \"") != NULL;
}

void check_clipboard_and_save(wchar_t **last_text) {
    if (!OpenClipboard(NULL)) return;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return;
    }

    const wchar_t *clipboard_text_w = GlobalLock(hData);
    if (!clipboard_text_w) {
        CloseClipboard();
        return;
    }

    if (*last_text && wcscmp(*last_text, clipboard_text_w) == 0) {
        GlobalUnlock(hData);
        CloseClipboard();
        return;
    }

    free(*last_text);
    *last_text = _wcsdup(clipboard_text_w);

    char content_utf8[65536] = {0};
    WideCharToMultiByte(CP_UTF8, 0, clipboard_text_w, -1, content_utf8, sizeof(content_utf8), NULL, NULL);

    printf("🔍 Найден новый текст в буфере обмена. Анализ...\n");

    if (is_c_file(content_utf8)) {
        printf("📂 Распознан как: .c файл\n");
        save_to_file(clipboard_text_w, "c");
    } else if (is_h_file(content_utf8)) {
        printf("📂 Распознан как: .h файл\n");
        save_to_file(clipboard_text_w, "h");
    } else {
        printf("⚠ Не удалось определить тип файла. Сохранение отменено.\n");
    }

    GlobalUnlock(hData);
    CloseClipboard();

    if (config.clear_clipboard_after_save) {
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            CloseClipboard();
            printf("🧹 Буфер обмена очищен.\n");
        }
    }
}

// --- Конфиг и автозапуск ---

void load_config(AppConfig *config) {
    config->clear_clipboard_after_save = GetPrivateProfileIntA("General", "clear_clipboard_after_save", 1, CONFIG_FILE);
    config->poll_interval_ms = GetPrivateProfileIntA("General", "poll_interval_ms", 2000, CONFIG_FILE);
    config->autostart = GetPrivateProfileIntA("General", "autostart", 0, CONFIG_FILE);
}

void enable_autostart_if_needed(const AppConfig *config) {
    if (!config->autostart) return;

    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, sizeof(path))) {
        printf("⚠ Не удалось получить путь к EXE.\n");
        return;
    }

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "ClipboardWatcher", 0, REG_SZ, (const BYTE *)path, (DWORD)(strlen(path) + 1));
        RegCloseKey(hKey);
        printf("🛠️ Автозапуск включён через реестр.\n");
    } else {
        printf("❌ Не удалось установить автозапуск через реестр.\n");
    }
}

// --- Командная строка ---

void print_help() {
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

void print_status() {
    printf("\n🔎 Текущий статус:\n");
    printf("  Префикс: %s\n", prefix_enabled ? prefix : "(отключён)");
    printf("  Настройки из конфигурации:\n");
    printf("    Очистка буфера после сохранения: %s\n", config.clear_clipboard_after_save ? "Вкл" : "Выкл");
    printf("    Интервал опроса (мс): %d\n", config.poll_interval_ms);
    printf("    Автозапуск: %s\n\n", config.autostart ? "Вкл" : "Выкл");
}

void clear_clipboard_manual() {
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        CloseClipboard();
        printf("🧹 Буфер обмена очищен вручную.\n");
    } else {
        printf("❌ Не удалось открыть буфер обмена для очистки.\n");
    }
}

bool set_prefix(const char *arg) {
    if (strlen(arg) >= MAX_FILENAME - 2) {
        printf("❌ Префикс слишком длинный.\n");
        return false;
    }
    int a, b;
    if (sscanf(arg, "%d.%d", &a, &b) == 2) {
        snprintf(prefix, MAX_FILENAME, "%d.%d_", a, b);
        prefix_enabled = true;
        printf("⚙️ Префикс установлен: %s\n", prefix);
        return true;
    }
    printf("❌ Некорректный формат префикса. Используйте пример: 1.2\n");
    return false;
}

unsigned __stdcall input_thread_func(void *arg) {
    (void)arg;
    char buffer[128];
    while (InterlockedCompareExchange(&running, 1, 1)) {
        if (fgets(buffer, sizeof(buffer), stdin)) {
            buffer[strcspn(buffer, "\r\n")] = 0;

            if (strcmp(buffer, "stop") == 0) {
                InterlockedExchange(&running, 0);
                printf("🛑 Получена команда остановки. Завершаем программу...\n");
                break;
            } else if (strcmp(buffer, "help") == 0) {
                print_help();
            } else if (strncmp(buffer, "prefix ", 7) == 0) {
                const char *arg = buffer + 7;
                if (strcmp(arg, "off") == 0) {
                    prefix_enabled = false;
                    prefix[0] = '\0';
                    printf("⚙️ Префикс отключён.\n");
                } else {
                    set_prefix(arg);
                }
            } else if (strcmp(buffer, "config reload") == 0) {
                load_config(&config);
                printf("🔄 Конфигурация перезагружена из %s\n", CONFIG_FILE);
            } else if (strcmp(buffer, "clear clipboard") == 0) {
                clear_clipboard_manual();
            } else if (strcmp(buffer, "status") == 0) {
                print_status();
            } else {
                printf("❓ Неизвестная команда. Введите 'help' для списка команд.\n");
            }
        }
    }
    return 0;
}

// --- Главная функция ---

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    setlocale(LC_ALL, "Russian_Russia.65001");

    load_config(&config);
    enable_autostart_if_needed(&config);

    wchar_t *last_text = NULL;

    printf("🚀 Программа запущена.\nНаберите 'help' для списка команд.\n\n");

    HANDLE hInputThread = (HANDLE)_beginthreadex(NULL, 0, input_thread_func, NULL, 0, NULL);

    while (InterlockedCompareExchange(&running, 1, 1)) {
        check_clipboard_and_save(&last_text);
        Sleep(config.poll_interval_ms);
    }

    free(last_text);
    WaitForSingleObject(hInputThread, INFINITE);
    CloseHandle(hInputThread);

    printf("👋 Программа завершена.\n");
    return 0;
}
