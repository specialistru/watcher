#pragma once
// Блок: Ввод команд и обработка
unsigned __stdcall input_thread_func(void* arg);
int  read_command_with_suggestion(char* out,size_t cap);
