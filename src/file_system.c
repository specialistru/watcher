#include "file_system.h"
#include <windows.h>

int file_exists(const char* filename){ DWORD a=GetFileAttributesA(filename); return (a!=INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)); }
