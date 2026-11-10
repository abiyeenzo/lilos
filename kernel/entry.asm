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

global isr_stub_128
isr_stub_128:
    push dword 0
    push dword 128
    jmp isr_common

; Entree en ring 3 : void user_enter(eip, esp_utilisateur). Retourne quand le
; processus se termine (user_return restaure le contexte du noyau).
global user_enter
user_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_kesp], esp
    mov eax, [esp + 20]
    mov ecx, [esp + 24]
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    push dword 0x23
    push ecx
    push dword 0x202
    push dword 0x1B
    push eax
    iret

; void user_return(int status) : abandonne la pile courante et revient a
; l'appelant de user_enter avec status en valeur de retour.
global user_return
user_return:
    mov eax, [esp + 4]
    mov esp, [saved_kesp]
    mov dx, 0x10
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    cld
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

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

section .bss
saved_kesp: resd 1

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dd isr_stub_ %+ i
%assign i i+1
%endrep
