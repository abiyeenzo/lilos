# LilOS

Petit système d'exploitation x86 32 bits écrit from scratch : bootloader en assembleur NASM, kernel en C, shell interactif.

## Contenu

- `bootloader/bootloader.asm` : secteur de boot BIOS. Charge le kernel par LBA (INT 13h étendu), active A20, installe une GDT et passe en mode protégé.
- `kernel/entry.asm` : point d'entrée, mise à zéro du BSS, stubs ISR/IRQ, rechargement GDT et IDT.
- `kernel/` : GDT, IDT, PIC 8259, timer PIT (100 Hz), clavier PS/2 (Maj, Verr Maj, Ctrl, flèches), paging PAE avec W^X (code en lecture seule, données non exécutables via le bit NX, page 0 et garde de pile non mappées), double fault sur pile dédiée, allocateur `kmalloc`/`kfree`, horloge CMOS, affichage VGA avec défilement et curseur matériel, sortie série COM1, `kprintf`.
- `kernel/shell.c` : shell en français avec historique (flèches haut et bas), saisie UTF-8 accentuée et les commandes `aide`, `effacer`, `echo`, `version`, `temps`, `date`, `memoire`, `cpuid`, `couleur`, `pause`, `hexdump`, `memtest`, `historique`, `redemarrer`, `eteindre`, `panic`, `proteger`, `ls`, `ecrire`, `ajouter`, `afficher`, `copier`, `supprimer` et `calculer`. Les anciens noms anglais (`help`, `clear`, `cat`, `rm`, `calc`...) restent acceptés. `nulltest`, `stacktest` et `proteger` déclenchent volontairement une exception pour tester les protections.
- Clavier AZERTY par défaut (AltGr compris : `@ # { [ | \ ^ ] }`). L'affichage VGA convertit l'UTF-8 vers la page de code 437 : é è à ç ù ê ô î etc.

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

Chaque push lance un fuzzer de l'allocateur, compile l'image, la démarre dans QEMU et pilote le shell au clavier (via le moniteur QEMU) en lisant la sortie série. Le clavier AZERTY et les accents sont testés en tapant de vraies touches. Les protections sont testées avec un CPU sans NX (`pentium3`) et avec NX (`max`).

## Licence

MIT
