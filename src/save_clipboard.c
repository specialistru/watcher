#include "save_clipboard.h"
#include "app_state.h"
#include "safe_str.h"
#include "name_utils.h"
#include "file_system.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void generate_unique_filename(const char* base,const char* ext,char* out){ AppState* S=app_state(); char pref[256]="",b[256]="",e[32]=""; if(S->prefix_enabled) z_buf_copy(pref,sizeof(pref),S->prefix); z_buf_copy(b,sizeof(b), base?base:"file"); z_buf_copy(e,sizeof(e), ext?ext:"txt"); build_name(out,MAX_FILENAME,pref,b,"",e); int idx=0; char id[32]; while(file_exists(out)){ _snprintf(id,sizeof(id),"_%d",idx++); build_name(out,MAX_FILENAME,pref,b,id,e);} out[MAX_FILENAME-1]='\0'; }

static void save_to_file_w(const wchar_t* w,const char* ext){ int u8len=WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL); if(u8len<=0) return; char* u8=(char*)malloc(u8len); if(!u8) return; WideCharToMultiByte(CP_UTF8,0,w,-1,u8,u8len,NULL,NULL);
 char base[256]={0}, fname[256]={0}; extract_filename_from_text(u8,ext,base,sizeof(base)); update_prefix_for_base(base,ext); generate_unique_filename(base,ext,fname);
 FILE* f=fopen(fname,"wb"); if(f){ static const unsigned char bom[]={0xEF,0xBB,0xBF}; fwrite(bom,sizeof(bom),1,f); fputs(u8,f); fclose(f); printf("✅ Сохранено: %s\n",fname);} else{ printf("❌ Ошибка сохранения.\n"); } free(u8); }

int is_h_file(const char* t){ const char* a=strstr(t,"#ifndef"); const char* b=strstr(t,"#define"); const char* c=strstr(t,"#endif"); int guards=(a&&b&&c); int once=(strstr(t,"#pragma once")!=NULL); return guards||once; }
int is_c_file(const char* t){ if(is_h_file(t)) return 0; if(strstr(t,"#include \"")||strstr(t,"int main(")||strstr(t,"for (")||strstr(t,"while (")||strstr(t,"return")) return 1; return 0; }

void check_clipboard_and_save(wchar_t** last){ if(!OpenClipboard(NULL)) return; HANDLE h=GetClipboardData(CF_UNICODETEXT); if(!h){ CloseClipboard(); return; }
 const wchar_t* w=(const wchar_t*)GlobalLock(h); if(!w){ CloseClipboard(); return; }
 if(*last && wcscmp(*last,w)==0){ GlobalUnlock(h); CloseClipboard(); return; }
 free(*last); *last=_wcsdup(w);
 char u8[65536]={0}; WideCharToMultiByte(CP_UTF8,0,w,-1,u8,sizeof(u8),NULL,NULL); printf("🔍 Новый текст. Анализ...\n");
 if(is_c_file(u8)){ printf("📂 Тип: .c\n"); save_to_file_w(w,"c"); }
 else if(is_h_file(u8)){ printf("📂 Тип: .h\n"); save_to_file_w(w,"h"); }
 else{ printf("⚠ Не удалось определить тип.\n"); }
 GlobalUnlock(h); CloseClipboard();
 AppState* S=app_state(); if(S->cfg.clear_clipboard_after_save) if(OpenClipboard(NULL)){ EmptyClipboard(); CloseClipboard(); printf("🧹 Буфер очищен.\n"); }
}
