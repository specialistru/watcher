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
#define MAX_PREFIX_LEN 32

volatile LONG running = 1;

typedef struct {
    int clear_clipboard_after_save;
    int poll_interval_ms;
    int autostart;
    char prefix[MAX_PREFIX_LEN]; // хранит префикс, либо пустую строку
} AppConfig;

AppConfig config = {0};

unsigned __stdcall input_thread_func(void *arg) {
    (void)arg;
    char buffer[128];

    while (InterlockedCompareExchange(&running, 1, 1)) {
        if (fgets(buffer, sizeof(buffer), stdin)) {
            buffer[strcspn(buffer, "\r\n")] = 0; // Удаляем символы перевода строки

            if (strcmp(buffer, "stop") == 0) {
                InterlockedExchange(&running, 0);
                printf("🛑 Получена команда остановки. Завершаем программу...\n");
                break;
            } else if (strncmp(buffer, "prefix ", 7) == 0) {
                char *arg = buffer + 7;
                if (strcmp(arg, "off") == 0) {
                    config.prefix[0] = '\0';
                    printf("🔧 Префикс отключён.\n");
                } else {
                    // Проверяем формат: должен быть вида X.Y где X и Y - цифры или буквы, но не обязательно
                    // Для простоты — просто копируем до MAX_PREFIX_LEN-2 и добавляем '_'
                    size_t len = strlen(arg);
                    if (len > 0 && len < MAX_PREFIX_LEN - 1) {
                        // Добавим _ в конец, если его нет
                        if (arg[len - 1] != '_') {
                            snprintf(config.prefix, MAX_PREFIX_LEN, "%s_", arg);
                        } else {
                            strncpy(config.prefix, arg, MAX_PREFIX_LEN);
                            config.prefix[MAX_PREFIX_LEN - 1] = '\0';
                        }
                        printf("🔧 Префикс установлен: \"%s\"\n", config.prefix);
                    } else {
                        printf("⚠ Некорректный префикс. Максимальная длина %d символов.\n", MAX_PREFIX_LEN - 1);
                    }
                }
            } else if (strcmp(buffer, "help") == 0) {
                printf("\n📖 Доступные команды:\n\n");
                printf("┌────────────────────┬──────────────────────────────────────────────────────────────┐\n");
                printf("│ %-18s │ %-64s │\n", "Команда", "Описание");
                printf("├────────────────────┼──────────────────────────────────────────────────────────────┤\n");
                printf("│ %-18s │ %-64s │\n", "help", "Показать эту справку");
                printf("│ %-18s │ %-64s │\n", "stop", "Завершить выполнение программы");
                printf("│ %-18s │ %-64s │\n", "prefix X.Y", "Установить префикс для имени файла, например: 1.2_");
                printf("│ %-18s │ %-64s │\n", "prefix off", "Отключить префикс");
                printf("│ %-18s │ %-64s │\n", "status", "Показать текущие настройки и префикс");
                printf("└────────────────────┴──────────────────────────────────────────────────────────────┘\n\n");
            } else if (strcmp(buffer, "status") == 0) {
                printf("\n🔍 Текущий статус:\n");
                printf("  Префикс для файлов: \"%s\"\n", config.prefix[0] ? config.prefix : "(отсутствует)");
                printf("  Очистка буфера после сохранения: %s\n", config.clear_clipboard_after_save ? "Включена" : "Выключена");
                printf("  Интервал опроса (мс): %d\n", config.poll_interval_ms);
                printf("  Автозапуск: %s\n\n", config.autostart ? "Включён" : "Выключен");
            } else {
                printf("❓ Неизвестная команда: \"%s\". Введите 'help' для списка команд.\n", buffer);
            }
        }
    }
    return 0;
}

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

void generate_unique_filename(const char *base, const char *ext, char *out) {
    int index = 0;
    snprintf(out, MAX_FILENAME, "%s.%s", base, ext);
    while (file_exists(out)) {
        snprintf(out, MAX_FILENAME, "%s_%d.%s", base, index++, ext);
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

    // Формируем имя с префиксом, если он установлен
    char base_with_prefix[MAX_FILENAME] = {0};
    if (config.prefix[0]) {
        snprintf(base_with_prefix, MAX_FILENAME, "%s%s", config.prefix, base);
    } else {
        strncpy(base_with_prefix, base, MAX_FILENAME);
    }

    generate_unique_filename(base_with_prefix, ext, filename);

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

void load_config(AppConfig *config) {
    config->clear_clipboard_after_save = GetPrivateProfileIntA("General", "clear_clipboard_after_save", 1, CONFIG_FILE);
    config->poll_interval_ms = GetPrivateProfileIntA("General", "poll_interval_ms", 2000, CONFIG_FILE);
    config->autostart = GetPrivateProfileIntA("General", "autostart", 0, CONFIG_FILE);
    config->prefix[0] = '\0'; // по умолчанию префикс отсутствует
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

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    setlocale(LC_ALL, "Russian_Russia.65001");

    load_config(&config);
    enable_autostart_if_needed(&config);

    wchar_t *last_text = NULL;

    printf("🚀 Программа запущена.\nНажимайте \"Копировать\" в ChatGPT или редакторе...\nВведите \"stop\" и нажмите Enter для выхода.\n");
    printf("Для помощи введите команду: help\n");

    uintptr_t thread_handle = _beginthreadex(NULL, 0, input_thread_func, NULL, 0, NULL);

    while (InterlockedCompareExchange(&running, 1, 1)) {
        check_clipboard_and_save(&last_text);
        Sleep(config.poll_interval_ms);
    }

    WaitForSingleObject((HANDLE)thread_handle, INFINITE);
    CloseHandle((HANDLE)thread_handle);

    free(last_text);
    printf("✅ Программа завершена.\n");
    return 0;
}
