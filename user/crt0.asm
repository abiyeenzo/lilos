; Point d'entree des programmes utilisateur : main(argc, argv) puis exit(code).
bits 32
section .text.start
global _start
extern main
_start:
    pop eax
    mov edx, esp
    push edx
    push eax
    call main
    mov ebx, eax
    xor eax, eax
    int 0x80
.hang:
    jmp .hang
