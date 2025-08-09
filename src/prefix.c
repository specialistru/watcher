#include "prefix.h"
#include "app_state.h"
#include "safe_str.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

bool set_package(const char* arg){ AppState* S=app_state(); int a; if(!arg||sscanf(arg,"%d",&a)!=1||a<0){ puts("❌ Некорректный номер пакета. Пример: package 5"); return false;} S->prefix_A=a; S->prefix_B=1; S->curr_base[0]='\0'; S->curr_mask=0; _snprintf(S->prefix,MAX_FILENAME,"%02d.%02d_",S->prefix_A,S->prefix_B); S->prefix_enabled=true; printf("📦 Пакет: A=%02d, B=%02d → %s\n",S->prefix_A,S->prefix_B,S->prefix); return true; }

bool set_prefix(const char* arg){ AppState* S=app_state(); if(!arg){puts("❌ Префикс пуст."); return false;} int a,b; if(sscanf(arg,"%d.%d",&a,&b)!=2){ puts("❌ Формат: A.B"); return false;} S->prefix_A=a; S->prefix_B=b; S->curr_base[0]='\0'; S->curr_mask=0; _snprintf(S->prefix,MAX_FILENAME,"%02d.%02d_",a,b); S->prefix_enabled=true; printf("⚙️ Префикс: %s\n", S->prefix); return true; }

void update_prefix_for_base(const char* base,const char* ext){ AppState* S=app_state(); if(!S->prefix_enabled||!base||!*base) return; if(S->curr_base[0]=='\0'){ z_buf_copy(S->curr_base,sizeof(S->curr_base),base); S->curr_mask=0; }
 else if(strcmp(base,S->curr_base)!=0){ ++S->prefix_B; _snprintf(S->prefix,MAX_FILENAME,"%02d.%02d_",S->prefix_A,S->prefix_B); z_buf_copy(S->curr_base,sizeof(S->curr_base),base); S->curr_mask=0; }
 if(ext&&*ext){ if(ext[0]=='h'||ext[0]=='H') S->curr_mask|=1u; if(ext[0]=='c'||ext[0]=='C') S->curr_mask|=2u; }
}

int adopt_next_B_from_fs(void){ AppState* S=app_state(); WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA("*.*",&fd); int maxB=0; if(h==INVALID_HANDLE_VALUE) return 1; do{ if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; const char* dot=strrchr(fd.cFileName,'.'); if(!dot||(_stricmp(dot,".h")&&_stricmp(dot,".c"))) continue; int A=0,B=0; if(sscanf(fd.cFileName,"%d.%d_",&A,&B)==2 && A==S->prefix_A){ if(B>maxB) maxB=B; } }while(FindNextFileA(h,&fd)); FindClose(h); return maxB+1; }
