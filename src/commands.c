#include "commands.h"
#include "utf_console.h"
#include "safe_str.h"
#include "app_state.h"
#include "app_config.h"
#include "reindex.h"
#include "ui_help.h"
#include "prefix.h"
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <conio.h>
#include <string.h>

static const char* COMMANDS[]={"help","stop","prefix X.Y","prefix off","prefix adopt","package X","reindex","reindex X","config reload","clear clipboard","status"};
#define NCOMMANDS (int)(sizeof(COMMANDS)/sizeof(COMMANDS[0]))
static const char* find_suggestion(const char* typed){ for(int i=0;i<NCOMMANDS;++i) if(istarts_with(COMMANDS[i],typed)) return COMMANDS[i]; return NULL; }
static void render_prompt(const char* prompt,const char* buf,const char* sg){ fputs("\r",stdout); fputs(prompt,stdout); fputs(buf,stdout); clear_eol(); if(sg){ size_t bl=strlen(buf),sl=strlen(sg); if(sl>bl){ const char* tail=sg+bl; fputs("\x1b[90m",stdout); fputs(tail,stdout); fputs("\x1b[0m",stdout); printf("\x1b[%zuD",strlen(tail)); }} fflush(stdout);}

int read_command_with_suggestion(char* out,size_t cap){ const char* PROMPT="> "; char buf[256]={0}; size_t len=0; render_prompt(PROMPT,buf,find_suggestion(buf)); for(;;){ int ch=_getch(); if(ch=='\r'||ch=='\n'){ const char* s=find_suggestion(buf); if(s && _stricmp(s,buf)!=0 && istarts_with(s,buf)){ str_copy_trunc(buf,sizeof(buf),s); len=strlen(buf);} render_prompt(PROMPT,buf,NULL); fputs("\n",stdout); str_copy_trunc(out,cap,buf); return 1; } if(ch==8){ if(len) buf[--len]='\0'; render_prompt(PROMPT,buf,find_suggestion(buf)); continue;} if(ch==9){ const char* s=find_suggestion(buf); if(s && _stricmp(s,buf)!=0 && istarts_with(s,buf)){ str_copy_trunc(buf,sizeof(buf),s); len=strlen(buf);} render_prompt(PROMPT,buf,find_suggestion(buf)); continue;} if(ch==27){ buf[0]='\0'; len=0; render_prompt(PROMPT,buf,find_suggestion(buf)); continue;} if(ch==0||ch==224){ (void)_getch(); continue;} if(ch>=32 && ch<127){ if(len+1<sizeof(buf)){ buf[len++]=(char)ch; buf[len]='\0'; } render_prompt(PROMPT,buf,find_suggestion(buf)); }} }

unsigned __stdcall input_thread_func(void* arg){ (void)arg; enable_vt_colors(); char line[256]; AppState* S=app_state(); for(;;){ if(!InterlockedCompareExchange(&S->running,1,1)) break; if(!read_command_with_suggestion(line,sizeof(line))){ InterlockedExchange(&S->running,0); break; }
 char* p=line; while(*p==' '||*p=='\t') ++p; if(!*p) continue; if(_stricmp(p,"stop")==0){ InterlockedExchange(&S->running,0); puts("🛑 Остановка..."); break; }
 else if(_stricmp(p,"help")==0){ print_help(); }
 else if(_strnicmp(p,"reindex",7)==0){ const char* q=p+7; while(*q==' '||*q=='\t') ++q; int dry=(strstr(q,"--dry-run")!=NULL); if(*q=='\0'||*q=='-'){ reindex_workspace(dry);} else { int A=-1; if(sscanf(q,"%d",&A)==1 && A>=0) reindex_repackage(A,dry); else puts("Использование: reindex [--dry-run] | reindex A [--dry-run]"); }}
 else if(_stricmp(p,"config reload")==0){ load_config(&S->cfg); printf("🔄 Конфигурация перезагружена.\n"); }
 else if(_stricmp(p,"clear clipboard")==0){ clear_clipboard_manual(); }
 else if(_stricmp(p,"status")==0){ print_status(); }
 else if(_strnicmp(p,"package ",8)==0){ const char* a=p+8; while(*a==' '||*a=='\t') ++a; set_package(a); }
 else if(_stricmp(p,"prefix adopt")==0){ if(!S->prefix_enabled){ puts("Сначала задайте prefix A.B"); continue;} S->prefix_B=adopt_next_B_from_fs(); _snprintf(S->prefix,MAX_FILENAME,"%02d.%02d_",S->prefix_A,S->prefix_B); printf("🔢 Следующий B=%02d → %s\n",S->prefix_B,S->prefix); }
 else if(_strnicmp(p,"prefix ",7)==0){ const char* a=p+7; while(*a==' '||*a=='\t') ++a; if(_stricmp(a,"off")==0){ S->prefix_enabled=false; S->prefix[0]='\0'; puts("⚙️ Префикс отключён."); } else { set_prefix(a);} }
 else { puts("❓ Неизвестная команда. 'help' — список команд."); } }
 return 0; }
