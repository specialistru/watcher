gcc -S -masm=intel -o clipboard_watcher.asm clipboard_watcher.c

оптимизированный ASM — добавь -O2 или -O3:
gcc -S -O2 -masm=intel clipboard_watcher.c -o clipboard_watcher.asm

с комментариями, можешь также собрать objdump:

gcc -g clipboard_watcher.c -o watcher.exe
objdump -d -M intel watcher.exe > disasm.txt
