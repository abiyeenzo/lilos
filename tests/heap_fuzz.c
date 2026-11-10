/* Test hote de l'allocateur : fuzz aleatoire + attaques. Build: make test-heap */
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "lilos.h"

static jmp_buf jb;
static int panicked;
void panic(const char *m) { (void)m; panicked = 1; longjmp(jb, 1); }

#define SLOTS 256
#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

int main(void) {
    size_t size = 4u << 20;
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED) FAIL("mmap");
    uint32_t lo = (uint32_t)(uintptr_t)mem, hi = lo + size;

    srand(1234);
    for (int round = 0; round < 200; round++) {
        heap_init(lo, hi);
        void *p[SLOTS] = {0};
        size_t len[SLOTS] = {0};
        unsigned char tag[SLOTS];
        for (int it = 0; it < 5000; it++) {
            int i = rand() % SLOTS;
            if (p[i]) {
                for (size_t j = 0; j < len[i]; j++)
                    if (((unsigned char *)p[i])[j] != tag[i]) FAIL("contenu altere (round %d)", round);
                kfree(p[i]);
                p[i] = NULL;
            } else {
                len[i] = 1 + rand() % 3000;
                p[i] = kmalloc(len[i]);
                if (!p[i]) continue;
                if ((uintptr_t)p[i] % 8) FAIL("alignement");
                if ((uint32_t)(uintptr_t)p[i] < lo || (uint32_t)(uintptr_t)p[i] + len[i] > hi) FAIL("hors zone");
                tag[i] = (unsigned char)rand();
                for (size_t j = 0; j < len[i]; j++) ((unsigned char *)p[i])[j] = tag[i];
            }
        }
        for (int i = 0; i < SLOTS; i++) if (p[i]) kfree(p[i]);
        struct heap_stats s;
        heap_stats(&s);
        if (s.used != 0 || s.blocks != 1) FAIL("fuite/fragmentation: used=%u blocks=%u", s.used, s.blocks);
    }
    printf("fuzz: ok\n");

    heap_init(lo, hi);
    if (kmalloc(0xFFFFFFFFu) || kmalloc(0xFFFFFFF9u) || kmalloc(size) || kmalloc(0))
        FAIL("tailles invalides acceptees");
    printf("tailles extremes: ok\n");

    /* double free, pointeur invalide, en-tete ecrase doivent paniquer */
    void *a = kmalloc(64);
    kfree(a);
    panicked = 0;
    if (!setjmp(jb)) kfree(a);
    if (!panicked) FAIL("double free non detecte");

    panicked = 0;
    if (!setjmp(jb)) kfree((void *)(uintptr_t)(lo + 3));
    if (!panicked) FAIL("pointeur invalide non detecte");

    panicked = 0;
    if (!setjmp(jb)) kfree((void *)(uintptr_t)(hi + 64));
    if (!panicked) FAIL("pointeur hors heap non detecte");

    heap_init(lo, hi);
    unsigned char *x = kmalloc(32);
    void *y = kmalloc(32);
    (void)y;
    for (int i = 0; i < 40; i++) x[i] = 0x41;       /* debordement dans l'en-tete suivant */
    panicked = 0;
    if (!setjmp(jb)) { kmalloc(8); kfree(x); }
    if (!panicked) FAIL("en-tete ecrase non detecte");
    printf("attaques: ok\n");
    return 0;
}
