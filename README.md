# LilOS

Petit système d'exploitation x86 32 bits écrit from scratch : bootloader en assembleur NASM, kernel en C, shell interactif.

## Contenu

- `bootloader/bootloader.asm` : secteur de boot BIOS. Charge le kernel par LBA (INT 13h étendu), active A20, installe une GDT et passe en mode protégé.
- `kernel/entry.asm` : point d'entrée, mise à zéro du BSS, stubs ISR/IRQ, rechargement GDT et IDT.
- `kernel/` : GDT, IDT, PIC 8259, timer PIT (100 Hz), clavier PS/2 (Maj, Verr Maj, Ctrl, flèches), paging (identité sur 16 Mo, page 0 protégée), allocateur `kmalloc`/`kfree`, horloge CMOS, affichage VGA avec défilement et curseur matériel, sortie série COM1, `kprintf`.
- `kernel/shell.c` : shell avec historique (flèches haut et bas) et les commandes `help`, `clear`, `echo`, `version`, `uptime`, `date`, `mem`, `cpuid`, `color`, `sleep`, `hexdump`, `memtest`, `history`, `reboot`, `halt`, `panic`.

Les exceptions CPU affichent les registres puis arrêtent la machine.

## Compiler et lancer

Dépendances : `nasm`, `gcc` avec support 32 bits (`gcc-multilib`), `binutils`, `qemu-system-i386`.

```
make            # produit build/lilos.img
make run        # lance dans QEMU
make run-serial # sans fenêtre, sortie sur le terminal
```

L'image est un disque brut : elle peut aussi être écrite sur une clé USB avec `dd` pour tester sur une machine en mode BIOS ou CSM.

## Organisation mémoire

| Adresse | Usage |
| --- | --- |
| `0x7C00` | bootloader |
| `0x10000` | kernel (code, données, BSS) |
| `0x90000` | sommet de la pile du kernel |
| `0x100000` - `0x1000000` | heap |

## CI

Chaque push compile l'image, la démarre dans QEMU en mode headless et vérifie sur le port série que le boot se termine sans exception.

## Licence

MIT
