#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>

#define MAX_FILENAME 256
#define CLEAR_CLIPBOARD_AFTER_SAVE 1 // Очистить буфер после обработки

int file_exists(const char *filename) {
    DWORD attr = GetFileAttributesA(filename);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void generate_unique_filename(const char* base, const char* ext, char* out) {
    int index = 0;
    sprintf(out, "%s.%s", base, ext);
    while (file_exists(out)) {
        sprintf(out, "%s_%d.%s", base, index++, ext);
    }
}

int is_c_file(const char* text) {
    return (
        strstr(text, "int main(") ||
        (strstr(text, "{") && strstr(text, "}")) ||  // реализация функций
        strstr(text, "return") ||
        strstr(text, "for (") || strstr(text, "while (") ||
        strstr(text, "=")
    );
}

int is_h_file(const char* text) {
    return (
        (strstr(text, "#ifndef") && strstr(text, "#define") && strstr(text, "#endif")) ||
        strstr(text, "#pragma once")
    );
}

void save_to_file(const char* content, const char* ext) {
    char filename[MAX_FILENAME];
    generate_unique_filename("output", ext, filename);

    FILE* file = fopen(filename, "w");
    if (file) {
        fputs(content, file);
        fclose(file);
        printf("✅ Сохранено: %s\n", filename);
    } else {
        printf("❌ Ошибка при сохранении файла.\n");
    }
}

void check_clipboard_and_save(char** last_text) {
    if (!OpenClipboard(NULL)) return;

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == NULL) {
        CloseClipboard();
        return;
    }

    char* clipboard_text = (char*)GlobalLock(hData);
    if (clipboard_text == NULL) {
        CloseClipboard();
        return;
    }

    if (*last_text && strcmp(*last_text, clipboard_text) == 0) {
        GlobalUnlock(hData);
        CloseClipboard();
        return; // Повтор, пропускаем
    }

    // Копируем буфер как новый
    free(*last_text);
    *last_text = _strdup(clipboard_text);

    printf("🔍 Найден новый текст в буфере обмена. Анализ...\n");

    if (is_c_file(clipboard_text)) {
        printf("📂 Распознан как: .c файл\n");
        save_to_file(clipboard_text, "c");
    } else if (is_h_file(clipboard_text)) {
        printf("📂 Распознан как: .h файл\n");
        save_to_file(clipboard_text, "h");
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
    char* last_text = NULL;

    printf("🚀 Программа запущена. Нажимайте \"Копировать\" в ChatGPT или любом редакторе...\n");

    while (1) {
        check_clipboard_and_save(&last_text);
        Sleep(2000); // Проверка каждые 2 секунды
    }

    free(last_text);
    return 0;
}
