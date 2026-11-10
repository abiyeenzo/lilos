#include "lilos.h"

/* Paging PAE : permet le bit NX. Identite sur 16 Mo avec des droits par page :
 *   code kernel          lecture + execution
 *   rodata               lecture seule, non executable
 *   donnees, pile, heap  lecture/ecriture, non executables
 *   page 0, garde pile   non mappees
 * CR0.WP est active : meme le kernel ne peut pas ecrire dans les pages en
 * lecture seule. Sans support NX du processeur, seule la partie ecriture
 * s'applique. */

#define PT_COUNT (RAM_MAP_END / (2u * 1024 * 1024))

#define P_PRESENT 1ull
#define P_RW      2ull
#define P_NX      (1ull << 63)

extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[];

static uint64_t pdpt[4] __attribute__((aligned(32)));
static uint64_t page_dir[512] __attribute__((aligned(4096)));
static uint64_t page_tables[PT_COUNT][512] __attribute__((aligned(4096)));
static int nx_on;

int paging_nx_enabled(void) { return nx_on; }

static int has_cpuid_feature(uint32_t leaf, int reg, int bit) {
    uint32_t r[4];
    cpuid_leaf(leaf & 0x80000000u, r);
    if (r[0] < leaf) return 0;
    cpuid_leaf(leaf, r);
    return (r[reg] >> bit) & 1;
}

void paging_init(void) {
    if (!has_cpuid_feature(1, 3, 6))
        panic("processeur sans PAE");
    nx_on = has_cpuid_feature(0x80000001, 3, 20);
    uint64_t nx = nx_on ? P_NX : 0;

    uint32_t text_lo = (uint32_t)__text_start, text_hi = (uint32_t)__text_end;
    uint32_t ro_lo = (uint32_t)__rodata_start, ro_hi = (uint32_t)__rodata_end;

    for (uint32_t t = 0; t < PT_COUNT; t++) {
        for (uint32_t i = 0; i < 512; i++) {
            uint32_t addr = (t * 512 + i) * 4096;
            uint64_t e = (uint64_t)addr | P_PRESENT | P_RW | nx;
            if (addr >= text_lo && addr < text_hi)
                e = (uint64_t)addr | P_PRESENT;
            else if (addr >= ro_lo && addr < ro_hi)
                e = (uint64_t)addr | P_PRESENT | nx;
            else if (addr == 0 || addr == STACK_GUARD)
                e = 0;
            page_tables[t][i] = e;
        }
        page_dir[t] = (uint64_t)(uint32_t)page_tables[t] | P_PRESENT | P_RW;
    }
    pdpt[0] = (uint64_t)(uint32_t)page_dir | P_PRESENT;

    uint32_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4 | 0x20));      /* PAE */

    if (nx_on) {
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
        __asm__ volatile ("wrmsr" : : "a"(lo | (1u << 11)), "d"(hi), "c"(0xC0000080));
    }

    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80010000, %%eax\n"                               /* PG + WP */
        "mov %%eax, %%cr0\n"
        : : "r"((uint32_t)pdpt) : "eax", "memory");
    tss_set_cr3((uint32_t)pdpt);
}
