; -----------------------------------------------------------------------------
; Bootloader minimal pour x86 (BIOS) en NASM (syntaxe Intel)
; Ce bootloader :
;  - Affiche le message "Démarrage OS minimal..."
;  - Charge 10 secteurs (après le bootloader) à l’adresse 0x1000 (où se trouve le kernel)
;  - Installe un GDT minimal
;  - Passe du mode réel au mode protégé et saute vers le kernel
; -----------------------------------------------------------------------------

; Indique à NASM que ce code est destiné à être chargé à l’adresse 0x7C00
org 0x7C00

; ----------------------------------------------------------------------
; Partie en mode réel (16 bits)
; ----------------------------------------------------------------------
[bits 16]

start:
    ; ------------------------------------------------------------------
    ; Affichage du message de démarrage via BIOS (INT 10h, fonction 0x0E)
    ; ------------------------------------------------------------------
    mov si, message         ; Charger l’adresse du message dans SI
.print_loop:
    lodsb                   ; Charger le caractère pointé par SI dans AL et incrémenter SI
    cmp al, 0               ; Vérifier la fin de chaîne (caractère nul)
    je .print_done          ; Si fin, sauter à la suite
    mov ah, 0x0E            ; Fonction "Teletype" d’INT 10h pour afficher le caractère
    int 0x10                ; Appeler l’interruption BIOS pour afficher le caractère
    jmp .print_loop         ; Boucler pour afficher le caractère suivant
.print_done:

    ; Sauvegarder le numéro de disque de démarrage (passé dans DL par le BIOS)
    mov [boot_drive], dl

    ; ------------------------------------------------------------------
    ; Chargement du kernel depuis le disque (10 secteurs)
    ; On suppose ici que le kernel est stocké immédiatement après le bootloader.
    ; Le kernel sera chargé à l’adresse 0x1000.
    ; ------------------------------------------------------------------
    mov bx, kernel_load_address  ; ES:BX = adresse de chargement (ici, ES = 0 par défaut)
    mov dh, 0                    ; Tête n°0
    mov dl, [boot_drive]         ; Utiliser le numéro de disque sauvegardé
    mov ah, 0x02                 ; Fonction "Lire secteurs" (INT 13h)
    mov al, 10                   ; Nombre de secteurs à lire (peut être ajusté)
    mov ch, 0                    ; Cylindre 0
    mov cl, 2                   ; Secteur de départ (le secteur 1 est le bootloader)
    int 0x13                   ; Appeler l’interruption BIOS pour lire depuis le disque
    jc disk_error             ; En cas d’erreur (CF = 1), sauter à la gestion d’erreur

    ; ------------------------------------------------------------------
    ; Installation d’un GDT minimal et passage en mode protégé
    ; ------------------------------------------------------------------

    ; Charger l’adresse du GDT dans la structure attendue par LGDT
    lgdt [gdt_descriptor]

    ; Activer le mode protégé en modifiant le registre CR0 :
    ; - Le bit PE (Protection Enable, bit 0) doit être mis à 1.
    mov eax, cr0
    or eax, 0x1              ; Définir le bit 0 (PE)
    mov cr0, eax

    ; Effectuer un saut lointain pour forcer le rechargement du segment CS avec le sélecteur du mode protégé.
    ; Le sélecteur 0x08 correspond à la deuxième entrée de la GDT (segment code 32 bits).
    jmp 0x08:protected_mode_start

; ----------------------------------------------------------------------
; Partie en mode protégé (32 bits)
; Il est impératif que les instructions suivantes soient compilées en 32 bits.
; ----------------------------------------------------------------------
[bits 32]
protected_mode_start:
    ; Mettre à jour les registres de segment pour utiliser le GDT nouvellement installé.
    mov ax, 0x10           ; Le sélecteur 0x10 correspond au segment data de la GDT
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Ici, on peut initialiser d’autres structures (IDT, etc.) ou effectuer d’autres
    ; opérations d’amorçage avant de passer le contrôle au kernel.
    ; Pour cet exemple, nous transférons simplement le contrôle au kernel chargé
    ; à l’adresse 0x1000.
    jmp kernel_load_address

; ----------------------------------------------------------------------
; Gestion d’erreur lors du chargement du kernel
; ----------------------------------------------------------------------
disk_error:
    ; Afficher un message d’erreur et se figer
    mov si, disk_error_msg
.error_print:
    lodsb
    cmp al, 0
    je halt_loop
    mov ah, 0x0E
    int 0x10
    jmp .error_print

halt_loop:
    cli                    ; Désactiver les interruptions
    hlt                    ; Arrêter le processeur
    jmp halt_loop          ; Boucle infinie

; ----------------------------------------------------------------------
; Données et définitions
; ----------------------------------------------------------------------
; Variable pour sauvegarder le numéro de disque de démarrage (passé dans DL par le BIOS)
boot_drive db 0

; Message de démarrage à afficher (terminé par un octet nul)
message db 'Démarrage OS minimal...', 0

; Message d’erreur en cas de problème lors du chargement du kernel
disk_error_msg db 'Erreur de chargement du kernel.', 0

; Adresse mémoire où sera chargé le kernel (modifiable selon votre projet)
kernel_load_address equ 0x1000

; ----------------------------------------------------------------------
; Table GDT minimale (3 entrées) :
;  - Entrée 0 : Descripteur nul (obligatoire)
;  - Entrée 1 : Segment code 32 bits couvrant 4 Go (0x08)
;  - Entrée 2 : Segment data 32 bits couvrant 4 Go (0x10)
; Les descripteurs sont définis en deux parties (DWORD/DWORD).
; ----------------------------------------------------------------------------
gdt_start:
    dq 0                           ; Descripteur nul

    ; Descripteur du segment code 32 bits
    ; base = 0, limite = 0xFFFFF, accès = 0x9A, flags = 0xCF
    ; Les 4 octets de poids faible contiennent (limite[15:0] et base[15:0])
    ; et les 4 octets suivants contiennent (base[23:16], accès, flags+limite[19:16], base[31:24])
    ; Ici, la valeur 0x0000FFFF et 0x00CF9A00 correspondent à ces paramètres.
    dd 0x0000FFFF                ; Limite (15:0) et base (15:0)
    dd 0x00CF9A00                ; Base (23:16), accès, flags/limite (19:16), base (31:24)

    ; Descripteur du segment data 32 bits
    ; base = 0, limite = 0xFFFFF, accès = 0x92, flags = 0xCF
    dd 0x0000FFFF                ; Limite (15:0) et base (15:0)
    dd 0x00CF9200                ; Base (23:16), accès, flags/limite (19:16), base (31:24)
gdt_end:

; Structure du GDT à utiliser avec LGDT.
; La structure attendue par LGDT est constituée d'un mot (taille du GDT - 1) suivi
; d'un DWORD contenant l’adresse linéaire de la table.
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; Limite du GDT (taille - 1)
    dd gdt_start                 ; Adresse de la table GDT

; -----------------------------------------------------------------------------
; Remplissage pour atteindre 510 octets, puis signature de boot (0x55AA)
; -----------------------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55
