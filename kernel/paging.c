#include "lilos.h"

#define PT_COUNT (RAM_MAP_END / (4096 * 1024))

static uint32_t page_dir[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[PT_COUNT][1024] __attribute__((aligned(4096)));

/* Identite sur les 16 premiers Mo. La page 0 reste non mappee pour
 * intercepter les dereferencements de NULL, ainsi qu'une page de garde
 * sous la pile du kernel. */
void paging_init(void) {
    for (uint32_t t = 0; t < PT_COUNT; t++) {
        for (uint32_t i = 0; i < 1024; i++) {
            uint32_t addr = (t * 1024 + i) * 4096;
            page_tables[t][i] = addr | 3;
        }
        page_dir[t] = (uint32_t)page_tables[t] | 3;
    }
    page_tables[0][0] = 0;
    page_tables[0][STACK_GUARD / 4096] = 0;     /* page de garde sous la pile */

    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"((uint32_t)page_dir) : "eax", "memory");
    tss_set_cr3((uint32_t)page_dir);
}
