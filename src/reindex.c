#include "reindex.h"
#include "safe_str.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

void plan_or_rename(const char* src,const char* dst,int dry){ if(strcmp(src,dst)==0){ printf("= %s (без изменений)\n",src); return;} if(dry){ printf("→ %s  ==>  %s\n",src,dst); return;} DWORD a=GetFileAttributesA(dst); if(a!=INVALID_FILE_ATTRIBUTES){ printf("❗ Цель существует: %s\n",dst); return;} if(MoveFileA(src,dst)) printf("✔ %s  ->  %s\n",src,dst); else printf("❌ Переименование: %s -> %s (код %lu)\n",src,dst,(unsigned long)GetLastError()); }

static void reindex_one(const char* fname,int dry){ int A=-1,B=-1; char rest[256]={0}; char stem[256]={0}; const char* dot=strrchr(fname,'.'); size_t L=dot?(size_t)(dot-fname):strlen(fname); if(L>=sizeof(stem)) L=sizeof(stem)-1; memcpy(stem,fname,L); stem[L]='\0'; if(sscanf(stem,"%d.%d_%255s",&A,&B,rest)==3 && A>=0 && B>=0 && rest[0]){ char dst[256]; _snprintf(dst,sizeof(dst),"%02d.%02d_%s%s",A,B,rest,dot?dot:""); plan_or_rename(fname,dst,dry); }}

void reindex_workspace(int dry){ WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA("*.h",&fd); if(h!=INVALID_HANDLE_VALUE){ do{ reindex_one(fd.cFileName,dry);}while(FindNextFileA(h,&fd)); FindClose(h);} h=FindFirstFileA("*.c",&fd); if(h!=INVALID_HANDLE_VALUE){ do{ reindex_one(fd.cFileName,dry);}while(FindNextFileA(h,&fd)); FindClose(h);} }

typedef struct{ char base[256]; char h[256]; char c[256]; } group_t;
static int cmp_groups(const void* a,const void* b){ return strcmp(((const group_t*)a)->base, ((const group_t*)b)->base); }

static void base_for_repack(const char* fname,char* out){ char stem[256]={0}; const char* dot=strrchr(fname,'.'); size_t L=dot?(size_t)(dot-fname):strlen(fname); if(L>=sizeof(stem)) L=sizeof(stem)-1; memcpy(stem,fname,L); stem[L]='\0'; const char* p=stem; int A,B; if(sscanf(stem,"%d.%d_%255s",&A,&B,out)==3){ out[255]='\0'; }
 else{ z_buf_copy(out,256, p); } }

void reindex_repackage(int A,int dry){ group_t g[2048]; int n=0; memset(g,0,sizeof(g)); WIN32_FIND_DATAA fd; HANDLE h;
#define ADD(is_h) do{ char base[256]; base_for_repack(fd.cFileName,base); int i; for(i=0;i<n;++i) if(!strcmp(g[i].base,base)) break; if(i==n){ if(n>=(int)(sizeof(g)/sizeof(g[0]))){ puts("❌ Слишком много групп"); break;} z_buf_copy(g[n].base,sizeof(g[n].base),base); g[n].h[0]=g[n].c[0]='\0'; ++n; } if(is_h) z_buf_copy(g[i].h,sizeof(g[i].h),fd.cFileName); else z_buf_copy(g[i].c,sizeof(g[i].c),fd.cFileName);}while(0)
    h=FindFirstFileA("*.h",&fd); if(h!=INVALID_HANDLE_VALUE){ do{ ADD(1); }while(FindNextFileA(h,&fd)); FindClose(h);} h=FindFirstFileA("*.c",&fd); if(h!=INVALID_HANDLE_VALUE){ do{ ADD(0); }while(FindNextFileA(h,&fd)); FindClose(h);} qsort(g,n,sizeof(g[0]),cmp_groups);
    for(int i=0,BB=1;i<n;++i,++BB){ char pref[16]; _snprintf(pref,sizeof(pref),"%02d.%02d_",A,BB); if(g[i].h[0]){ char dst[256]; _snprintf(dst,sizeof(dst),"%s%s.h",pref,g[i].base); plan_or_rename(g[i].h,dst,dry);} if(g[i].c[0]){ char dst[256]; _snprintf(dst,sizeof(dst),"%s%s.c",pref,g[i].base); plan_or_rename(g[i].c,dst,dry);} }
}
