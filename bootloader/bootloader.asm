; Bootloader x86 (BIOS) : charge le kernel par LBA, active A20,
; passe en mode protege 32 bits et saute au kernel.
; Assemble avec : nasm -f bin -DKERNEL_SECTORS=<n> bootloader.asm

%ifndef KERNEL_SECTORS
%error "KERNEL_SECTORS doit etre defini (-DKERNEL_SECTORS=n)"
%endif

KERNEL_SEG   equ 0x1000            ; segment de chargement (adresse physique 0x10000)
KERNEL_ADDR  equ 0x10000
CHUNK        equ 32                ; secteurs par lecture (16 Ko, evite les frontieres de 64 Ko)

org 0x7C00
bits 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    mov ax, 0x0003                 ; mode texte 80x25, efface l'ecran
    int 0x10

    mov si, msg_boot
    call print

    ; Les extensions INT 13h (lecture LBA) sont necessaires
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc  no_ext
    cmp bx, 0xAA55
    jne no_ext

    mov word [remaining], KERNEL_SECTORS
    mov dword [dap_lba], 1
    mov word [dap_seg], KERNEL_SEG

.load:
    mov ax, [remaining]
    test ax, ax
    jz  .loaded
    cmp ax, CHUNK
    jbe .count_ok
    mov ax, CHUNK
.count_ok:
    mov [dap_count], ax
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  disk_error
    movzx eax, word [dap_count]
    add [dap_lba], eax
    sub [remaining], ax
    shl ax, 5                      ; 32 paragraphes par secteur de 512 octets
    add [dap_seg], ax
    jmp .load

.loaded:
    ; A20 : tentative BIOS, puis porte rapide 0x92
    mov ax, 0x2401
    int 0x15
    in  al, 0x92
    test al, 2
    jnz .a20_ok
    or  al, 2
    and al, 0xFE
    out 0x92, al
.a20_ok:

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_start

; ---------------------------------------------------------------------------
; Utilitaires 16 bits
; ---------------------------------------------------------------------------
print:
    lodsb
    test al, al
    jz  .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

no_ext:
    mov si, msg_noext
    jmp fatal
disk_error:
    mov si, msg_disk
fatal:
    call print
.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Mode protege
; ---------------------------------------------------------------------------
bits 32
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov eax, KERNEL_ADDR
    jmp eax

; ---------------------------------------------------------------------------
; Donnees
; ---------------------------------------------------------------------------
bits 16
boot_drive: db 0
remaining:  dw 0

msg_boot:   db "LilOS: chargement du kernel...", 13, 10, 0
msg_noext:  db "Erreur: BIOS sans extensions LBA", 13, 10, 0
msg_disk:   db "Erreur: lecture disque", 13, 10, 0

align 4
dap:
    db 0x10, 0
dap_count:  dw 0
dap_off:    dw 0
dap_seg:    dw 0
dap_lba:    dq 0

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF          ; code 32 bits, base 0, limite 4 Go
    dq 0x00CF92000000FFFF          ; donnees 32 bits, base 0, limite 4 Go
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
