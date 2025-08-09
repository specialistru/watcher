#include "name_utils.h"
#include "safe_str.h"
#include <ctype.h>
#include <string.h>

void to_lowercase_and_sanitize(char* s){ if(!s) return; for(size_t i=0;s[i];++i){ unsigned c=(unsigned char)s[i]; s[i]=(char)((isalnum(c)||s[i]=='_')?tolower(c):'_'); }}
void strip_common_suffixes(char* base){ size_t len=base?strlen(base):0; if(len>2 && strcmp(&base[len-2],"_H")==0) base[len-2]='\0'; else if(len>3 && strcmp(&base[len-3],"__H")==0) base[len-3]='\0'; else if(len>8 && strcmp(&base[len-8],"_INCLUDED")==0) base[len-8]='\0'; }
void extract_filename_from_text(const char* text,const char* ext,char* out,size_t cap){ char base[256]={0}; if(ext && _stricmp(ext,"h")==0){ const char* ifn=strstr(text,"#ifndef "); const char* def=strstr(text,"#define "); if(ifn&&def){ const char* st=ifn+8; sscanf(st,"%255s",base); strip_common_suffixes(base);} }
 else if(ext && _stricmp(ext,"c")==0){ const char* p=text; while((p=strstr(p,"#include"))){ const char* q1=strchr(p,'"'); if(!q1){p+=8;continue;} ++q1; const char* q2=strchr(q1,'"'); if(!q2){p+=8;continue;} size_t n=(size_t)(q2-q1); if(n>0 && n<sizeof(base)){ memcpy(base,q1,n); base[n]='\0'; char* dot=strrchr(base,'.'); if(dot) *dot='\0'; break;} p=q2+1; }}
 if(base[0]=='\0') strcpy(base,"output"); to_lowercase_and_sanitize(base); z_buf_copy(out,cap,base); }
void build_name(char* dst,size_t cap,const char* pref,const char* base,const char* idx,const char* ext){ dst[0]='\0'; if(pref&&*pref) z_buf_cat(dst,cap,pref); size_t cur=z_strnlen(dst,cap); size_t extlen=z_strnlen(ext?ext:"",256); size_t rem=(cur<cap)?(cap-1-cur):0; size_t mintail=1+extlen; if(rem>mintail){ size_t room=rem-mintail; if(base&&*base&&room){ size_t nb=z_strnlen(base,room); if(nb){ char tmp[256]; memcpy(tmp,base,nb); tmp[nb]='\0'; z_buf_cat(dst,cap,tmp); room-=nb; }} if(idx&&*idx&&room){ size_t ni=z_strnlen(idx,room); if(ni){ char t2[64]; memcpy(t2,idx,ni); t2[ni]='\0'; z_buf_cat(dst,cap,t2);} }} z_buf_cat_char(dst,cap,'.'); z_buf_cat(dst,cap,ext?ext:""); }
