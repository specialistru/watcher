[{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'%s' directive output may be truncated writing up to 255 bytes into a region of size between 2 and 256 [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 116,
	"startColumn": 48,
	"endLineNumber": 116,
	"endColumn": 48
},{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'snprintf' output may be truncated before the last format character [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 120,
	"startColumn": 51,
	"endLineNumber": 120,
	"endColumn": 51
},{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'%s' directive output may be truncated writing up to 254 bytes into a region of size between 1 and 256 [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 125,
	"startColumn": 44,
	"endLineNumber": 125,
	"endColumn": 44
},{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'%s' directive output may be truncated writing up to 253 bytes into a region of size between 0 and 255 [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 138,
	"startColumn": 49,
	"endLineNumber": 138,
	"endColumn": 49
},{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'%s' directive output may be truncated writing up to 254 bytes into a region of size between 1 and 255 [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 147,
	"startColumn": 45,
	"endLineNumber": 147,
	"endColumn": 45
},{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'%s' directive output may be truncated writing up to 11 bytes into a region of size between 1 and 256 [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 167,
	"startColumn": 54,
	"endLineNumber": 167,
	"endColumn": 54
},{
	"resource": "/f:/Watcher/clipboard_watcher.c",
	"owner": "makefile-tools",
	"severity": 8,
	"message": "'%s' directive output may be truncated writing up to 11 bytes into a region of size between 1 and 256 [-Werror=format-truncation=]",
	"source": "gcc",
	"startLineNumber": 190,
	"startColumn": 52,
	"endLineNumber": 190,
	"endColumn": 52
}]


void generate_unique_filename(const char *base, const char *ext, char *out)
{
    int index = 0;
    size_t prefix_len = prefix_enabled ? strlen(prefix) : 0;
    size_t base_len = strlen(base);
    size_t ext_len = strlen(ext);

    // Максимум для целочисленного индекса (до 10 цифр)
    char index_str[12] = {0};

    if (prefix_enabled)
    {
        // Проверяем длину без индекса
        if (prefix_len + base_len + 1 + ext_len + 1 > MAX_FILENAME - 1)
        {
            // Обрезаем base, чтобы вместить в буфер
            size_t allowed_base_len = MAX_FILENAME - 1 - prefix_len - 1 - ext_len - 1;
            if (allowed_base_len > 0 && allowed_base_len < base_len)
            {
                // Копируем часть base
                char base_trunc[MAX_FILENAME];
                strncpy(base_trunc, base, allowed_base_len);
                base_trunc[allowed_base_len] = '\0';
                snprintf(out, MAX_FILENAME, "%s.%s", prefix, base_trunc, ext);
            }
            else
            {
                snprintf(out, MAX_FILENAME, "%s.%s", prefix, ext);
            }
        }
        else
        {
            snprintf(out, MAX_FILENAME, "%s.%s", prefix, base, ext);
        }
    }
    else
    {
        if (base_len + 1 + ext_len + 1 > MAX_FILENAME - 1)
        {
            // Обрезаем base
            size_t allowed_base_len = MAX_FILENAME - 1 - 1 - ext_len - 1;
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

    // Проверяем, существует ли файл
    while (file_exists(out))
    {
        snprintf(index_str, sizeof(index_str), "_%d", index++);
        size_t out_len = 0;

        if (prefix_enabled)
        {
            size_t len = prefix_len + base_len + strlen(index_str) + 1 + ext_len;
            if (len > MAX_FILENAME - 1)
            {
                // Обрезаем base для вмещения
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
