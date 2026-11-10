#include "lilos.h"

#define MAGIC_FREE 0xF2EEB10Cu
#define MAGIC_USED 0xA110C8EDu

struct block {
    uint32_t size;          /* taille utile, sans l'en-tete */
    uint32_t magic;
    struct block *next;
    struct block *prev;
};

#define HDR       ((uint32_t)sizeof(struct block))
#define MIN_SPLIT 16

static struct block *head;
static uint32_t heap_lo, heap_hi;

/* Section critique : le heap reste coherent si un handler d'IRQ alloue. */
#ifdef HOST_TEST
static inline uint32_t irq_save(void) { return 0; }
static inline void irq_restore(uint32_t f) { (void)f; }
#else
static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile ("push %0; popf" : : "r"(f) : "memory", "cc");
}
#endif

static void check(const struct block *b) {
    if ((uint32_t)b < heap_lo || (uint32_t)b + HDR > heap_hi ||
        (b->magic != MAGIC_FREE && b->magic != MAGIC_USED))
        panic("heap corrompu");
}

void heap_init(uint32_t start, uint32_t end) {
    heap_lo = start;
    heap_hi = end;
    head = (struct block *)start;
    head->size = end - start - HDR;
    head->magic = MAGIC_FREE;
    head->next = NULL;
    head->prev = NULL;
}

void *kmalloc(size_t n) {
    if (n == 0 || n > heap_hi - heap_lo) return NULL;
    uint32_t size = ((uint32_t)n + 7) & ~7u;
    void *res = NULL;
    uint32_t f = irq_save();

    for (struct block *b = head; b; b = b->next) {
        check(b);
        if (b->magic != MAGIC_FREE || b->size < size) continue;

        if (b->size - size >= HDR + MIN_SPLIT) {
            struct block *rest = (struct block *)((uint8_t *)b + HDR + size);
            rest->size = b->size - size - HDR;
            rest->magic = MAGIC_FREE;
            rest->next = b->next;
            rest->prev = b;
            if (b->next) b->next->prev = rest;
            b->next = rest;
            b->size = size;
        }
        b->magic = MAGIC_USED;
        res = (uint8_t *)b + HDR;
        break;
    }
    irq_restore(f);
    return res;
}

void *kcalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    uint32_t a = (uint32_t)p;
    if (a < heap_lo + HDR || a >= heap_hi || (a - heap_lo) % 8)
        panic("kfree: pointeur hors du heap");

    uint32_t f = irq_save();
    struct block *b = (struct block *)(a - HDR);
    check(b);
    if (b->magic == MAGIC_FREE)
        panic("kfree: double liberation");

    b->magic = MAGIC_FREE;
    if (b->next && b->next->magic == MAGIC_FREE) {
        check(b->next);
        b->size += HDR + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        check(b->prev);
        b->prev->size += HDR + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
    irq_restore(f);
}

void heap_stats(struct heap_stats *s) {
    s->used = s->free = s->blocks = 0;
    uint32_t f = irq_save();
    for (struct block *b = head; b; b = b->next) {
        check(b);
        s->blocks++;
        if (b->magic == MAGIC_FREE) s->free += b->size;
        else s->used += b->size;
    }
    irq_restore(f);
}
