; Point d'entree du kernel, stubs d'interruptions, rechargement GDT/IDT.

bits 32

extern kmain
extern isr_handler
extern __bss_start
extern __bss_end

section .text.entry
global _start
_start:
    cli
    mov esp, 0x90000
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .text

global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
    ret

global idt_flush
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; Les exceptions 8, 10-14, 17, 21, 29 et 30 empilent un code d'erreur.
%assign i 0
%rep 48
isr_stub_ %+ i:
%if (i == 8) || (i >= 10 && i <= 14) || (i == 17) || (i == 21) || (i == 29) || (i == 30)
%else
    push dword 0
%endif
    push dword i
    jmp isr_common
%assign i i+1
%endrep

isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dd isr_stub_ %+ i
%assign i i+1
%endrep
