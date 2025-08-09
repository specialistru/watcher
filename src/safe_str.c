#include "safe_str.h"
#include <string.h>
#include <stdio.h>

size_t z_strnlen(const char* s,size_t max){ size_t i=0; if(!s) return 0; for(;i<max && s[i];++i){} return i; }
void str_copy_trunc(char* dst,size_t cap,const char* src){ if(!dst||!cap){return;} if(!src){dst[0]='\0';return;} size_t n=strlen(src); if(n>=cap) n=cap-1; if(n) memcpy(dst,src,n); dst[n]='\0'; }
void z_buf_copy(char* dst,size_t cap,const char* src){ if(!dst||!cap){return;} if(!src){dst[0]='\0';return;} size_t n=(cap>0)?z_strnlen(src,cap-1):0; if(n) memcpy(dst,src,n); dst[n]='\0'; }
void z_buf_cat(char* dst,size_t cap,const char* src){ if(!dst||!cap) return; size_t cur=z_strnlen(dst,cap); if(cur>=cap-1) return; size_t rem=cap-1-cur; size_t n=z_strnlen(src?src:"",rem); if(n) memcpy(dst+cur,src,n); dst[cur+n]='\0'; }
void z_buf_cat_char(char* dst,size_t cap,char ch){ if(!dst||!cap) return; size_t cur=z_strnlen(dst,cap); if(cur>=cap-1) return; dst[cur]=ch; dst[cur+1]='\0'; }
int istarts_with(const char* s,const char* p){ while(*s && *p){ char a=*s,b=*p; if(a>='A'&&a<='Z') a+='a'-'A'; if(b>='A'&&b<='Z') b+='a'-'A'; if(a!=b) return 0; ++s;++p;} return *p=='\0'; }
void clear_eol(void){ fputs("\x1b[K",stdout); }
