#pragma once
// Блок: Реиндексация файлов
void reindex_workspace(int dry_run);
void reindex_repackage(int A,int dry_run);
void plan_or_rename(const char* src,const char* dst,int dry_run);
