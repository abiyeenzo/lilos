#include "ulib.h"

/* Programme hostile : chaque numero tente une attaque contre le noyau.
 * "REFUSE" = le noyau a refuse proprement, "BREACH" = l'attaque a reussi (bug).
 * Les attaques par instruction doivent tuer le processus sans toucher au noyau. */

static char big[64];
static volatile int sink;

static int atoi_(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

static void verdict(int r) {
    if (r < 0) puts_("REFUSE\n");
    else puts_("BREACH : appel accepte\n");
}

static int recurse(int n) {
    volatile char pad[256];
    pad[0] = (char)n;
    return recurse(n + 1) + pad[0];
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int n = atoi_(argv[1]);
    char name[8] = "x";

    switch (n) {
    case 0:  verdict(sys(SYS_WRITE, 1, 0x10000, 100, 0)); break;                 /* ecrire depuis le noyau */
    case 1:  verdict(sys(SYS_FREAD, (int)name, 0x10000, 100, 0)); break;         /* lire dans le noyau */
    case 2:  verdict(sys(SYS_FREAD, (int)name, 0x400FFFF6, 100, 0)); break;      /* tampon a cheval sur la fin */
    case 3:  verdict(sys(SYS_WRITE, 1, (int)0xFFFFFFF0, 0x20, 0)); break;        /* debordement d'adresse */
    case 4:  verdict(sys(SYS_WRITE, 1, (int)big, 0x7FFFFFFF, 0)); break;         /* longueur enorme */
    case 5:  verdict(sys(SYS_FREAD, 0x10000, (int)big, 10, 0)); break;           /* nom dans le noyau */
    case 6:  for (int i = 0; i < 64; i++) big[i] = 'A';                          /* nom sans NUL */
             verdict(sys(SYS_FREAD, (int)big, (int)big, 10, 0)); break;
    case 7:  verdict(sys(-1, 0, 0, 0, 0)); verdict(sys(1000, 0, 0, 0, 0)); verdict(sys((int)0x80000000, 0, 0, 0, 0)); break;
    case 8:  verdict(sys(SYS_READ, 0, (int)big, 1, 0)); break;                   /* lecture trop courte */
    case 9:  verdict(sys(SYS_READ, 1, (int)big, 10, 0)); break;                  /* mauvais descripteur */
    case 10: verdict(sys(SYS_READ, 0, (int)0x40000100, 100, 0)); break;          /* ecrire dans son propre code */
    case 11: verdict(sys(SYS_FWRITE, (int)name, 0x10000, 100, 0)); break;        /* fwrite depuis le noyau */
    case 12: verdict(sys(SYS_SLEEP, 0x7FFFFFFF, 0, 0, 0)); break;
    case 13: verdict(sys(SYS_FWRITE, (int)name, (int)big, 0x7FFFFFFF, 0)); break;
    /* instructions privilegiees : le CPU doit lever #GP et le processus etre tue */
    case 20: __asm__ volatile ("cli"); break;
    case 21: __asm__ volatile ("hlt"); break;
    case 22: __asm__ volatile ("outb %%al, $0x80" : : "a"(0)); break;
    case 23: { unsigned v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); sink = (int)v; } break;
    case 24: { unsigned long long t[1] = { 0 }; __asm__ volatile ("lgdt %0" : : "m"(t)); } break;
    case 25: { unsigned a, d; __asm__ volatile ("rdmsr" : "=a"(a), "=d"(d) : "c"(0xC0000080)); } break;
    case 26: __asm__ volatile ("mov %%ax, %%ds" : : "a"(0x10)); break;          /* charger un segment noyau */
    case 27: __asm__ volatile ("int $0x20"); break;                              /* IRQ simulee */
    case 28: __asm__ volatile ("int $0x0e"); break;                              /* page fault simule */
    case 29: __asm__ volatile ("int $3"); break;
    case 30: __asm__ volatile ("ljmp $0x08, $0x40000000"); break;                /* saut vers le segment noyau */
    case 31: __asm__ volatile ("xorl %%edx, %%edx\n\tmovl $1, %%eax\n\txorl %%ecx, %%ecx\n\tdivl %%ecx" : : : "eax", "ecx", "edx"); break;  /* division par zero */
    case 32: __asm__ volatile ("ud2"); break;
    case 33: __asm__ volatile ("pushf; orl $0x100, (%%esp); popf" : : : "cc"); break;  /* trap flag */
    case 34: __asm__ volatile ("jmp *%0" : : "r"(0x10000)); break;               /* sauter dans le noyau */
    case 35: __asm__ volatile ("jmp *%0" : : "r"(big)); break;                   /* executer des donnees (NX) */
    case 36: sink = recurse(0); break;                                           /* debordement de pile utilisateur */
    case 37: sink = *(volatile int *)0; break;
    case 38: __asm__ volatile ("wrmsr" : : "a"(0), "d"(0), "c"(0x174)); break;   /* ecrire un MSR (SYSENTER) */
    case 39: { unsigned v; __asm__ volatile ("rdtsc" : "=a"(v) : : "edx"); sink = (int)v; puts_("ok rdtsc\n"); } break;
    /* drapeaux CPU piegeux avant un appel systeme : le noyau doit rester coherent */
    case 40: __asm__ volatile ("std"); puts_("ecriture avec DF=1\n"); __asm__ volatile ("cld"); break;
    case 41: __asm__ volatile ("pushf; orl $0x4000, (%%esp); popf" : : : "cc");  /* NT */
             puts_("ecriture avec NT=1\n"); break;
    case 42: __asm__ volatile ("pushf; orl $0x40000, (%%esp); popf" : : : "cc"); /* AC */
             puts_("ecriture avec AC=1\n"); break;
    case 43: __asm__ volatile ("pushf; orl $0x3000, (%%esp); popf" : : : "cc");  /* IOPL=3 (doit etre ignore) */
             __asm__ volatile ("cli"); break;                                      /* doit encore faire #GP */
    case 44: for (int i = 0; i < 20000; i++) sink = getuid_(); puts_("20000 appels\n"); break;
    case 45: __asm__ volatile ("movl $-1, %%esp\n\tint $0x80" : : "a"(SYS_GETUID) : "memory"); break; /* esp invalide */
    /* acces aux ports d'E/S : espionner le clavier, reprogrammer le PIC, parler au bus PCI */
    case 46: { unsigned char v; __asm__ volatile ("inb $0x60, %0" : "=a"(v)); sink = v; puts_("BREACH : lu le port clavier 0x60\n"); } break;
    case 47: __asm__ volatile ("outl %%eax, %%dx" : : "a"(0x80000000), "d"(0xCF8)); puts_("BREACH : ecrit le port PCI 0xCF8\n"); break;
    case 48: __asm__ volatile ("outb %0, $0x20" : : "a"((unsigned char)0xFF)); puts_("BREACH : ecrit le PIC\n"); break;
    case 49: { unsigned char v; __asm__ volatile ("inb %%dx, %%al" : "=a"(v) : "d"(0x3F8)); sink = v; puts_("BREACH : lu le port serie\n"); } break;
    case 50: { unsigned char v; __asm__ volatile ("inb $0x40, %0" : "=a"(v)); sink = v; puts_("BREACH : lu le timer\n"); } break;
    default: puts_("attaque inconnue\n"); return 2;
    }
    puts_("FIN de l'attaque (le processus a survecu)\n");
    return 0;
}
