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
#define CLEAR_CLIPBOARD_AFTER_SAVE 1

volatile LONG running = 1;

unsigned __stdcall input_thread_func(void *arg) {
    (void)arg;
    char buffer[32];
    while (InterlockedCompareExchange(&running, 1, 1)) {
        if (fgets(buffer, sizeof(buffer), stdin)) {
            buffer[strcspn(buffer, "\r\n")] = 0;
            if (strcmp(buffer, "stop") == 0) {
                InterlockedExchange(&running, 0);
                printf("🛑 Получена команда остановки. Завершаем программу...\n");
                break;
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

void extract_filename_from_text(const char *text, const char *ext, char *out) {
    const char *start = NULL;
    char base[MAX_FILENAME] = {0};

    if (strcmp(ext, "h") == 0) {
        start = strstr(text, "#ifndef ");
        if (!start) start = strstr(text, "#define ");
        if (start) {
            start += 8;
            sscanf(start, "%[^ \n\r]", base);
            size_t len = strlen(base);
            if (len > 2 && strcmp(&base[len - 2], "_H") == 0) {
                base[len - 2] = '\0';
            }
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
    char content_utf8[65536] = {0};
    WideCharToMultiByte(CP_UTF8, 0, content_w, -1, content_utf8, sizeof(content_utf8), NULL, NULL);

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
}

bool is_c_file(const char *text) {
    return strstr(text, "int main(") || strstr(text, "return") ||
           strstr(text, "for (") || strstr(text, "while (") ||
           (strstr(text, "{") && strstr(text, "}"));
}

bool is_h_file(const char *text) {
    return (strstr(text, "#ifndef") && strstr(text, "#define") && strstr(text, "#endif")) ||
           strstr(text, "#pragma once");
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

#if CLEAR_CLIPBOARD_AFTER_SAVE
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        CloseClipboard();
        printf("🧹 Буфер обмена очищен.\n");
    }
#endif
}

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    setlocale(LC_ALL, "Russian_Russia.65001");

    wchar_t *last_text = NULL;

    printf("🚀 Программа запущена.\nНажимайте \"Копировать\" в ChatGPT или редакторе...\nВведите \"stop\" и нажмите Enter для выхода.\n");

    uintptr_t thread_handle = _beginthreadex(NULL, 0, input_thread_func, NULL, 0, NULL);

    while (InterlockedCompareExchange(&running, 1, 1)) {
        check_clipboard_and_save(&last_text);
        Sleep(2000);
    }

    WaitForSingleObject((HANDLE)thread_handle, INFINITE);
    CloseHandle((HANDLE)thread_handle);

    free(last_text);
    printf("✅ Программа завершена.\n");
    return 0;
}
