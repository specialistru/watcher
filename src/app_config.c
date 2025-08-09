#include "app_config.h"
#include <windows.h>
#include <stdio.h>

void load_config(AppConfig* cfg){
    if(!cfg) return;
    cfg->clear_clipboard_after_save = GetPrivateProfileIntA("General","clear_clipboard_after_save",1,CONFIG_FILE);
    cfg->poll_interval_ms           = GetPrivateProfileIntA("General","poll_interval_ms",2000,CONFIG_FILE);
    cfg->autostart                  = GetPrivateProfileIntA("General","autostart",0,CONFIG_FILE);
}

void enable_autostart_if_needed(const AppConfig* cfg){
    if(!cfg || !cfg->autostart) return;
    char path[MAX_PATH];
    if(!GetModuleFileNameA(NULL, path, sizeof(path))){
        printf("⚠ Не удалось получить путь к EXE.\n");
        return;
    }
    HKEY hKey;
    if(RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&hKey)==ERROR_SUCCESS){
        RegSetValueExA(hKey, "ClipboardWatcher", 0, REG_SZ, (const BYTE*)path, (DWORD)(lstrlenA(path)+1));
        RegCloseKey(hKey);
        printf("🛠️ Автозапуск включён через реестр.\n");
    }else{
        printf("❌ Не удалось установить автозапуск.\n");
    }
}
