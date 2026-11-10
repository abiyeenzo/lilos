#include "lilos.h"

struct block {
    uint32_t size;          /* taille utile, sans l'en-tete */
    uint32_t free;
    struct block *next;
    struct block *prev;
};

#define HDR   ((uint32_t)sizeof(struct block))
#define MIN_SPLIT 16

static struct block *head;

void heap_init(uint32_t start, uint32_t end) {
    head = (struct block *)start;
    head->size = end - start - HDR;
    head->free = 1;
    head->next = NULL;
    head->prev = NULL;
}

void *kmalloc(size_t n) {
    if (n == 0) return NULL;
    uint32_t size = ((uint32_t)n + 7) & ~7u;

    for (struct block *b = head; b; b = b->next) {
        if (!b->free || b->size < size) continue;

        if (b->size >= size + HDR + MIN_SPLIT) {
            struct block *rest = (struct block *)((uint8_t *)b + HDR + size);
            rest->size = b->size - size - HDR;
            rest->free = 1;
            rest->next = b->next;
            rest->prev = b;
            if (b->next) b->next->prev = rest;
            b->next = rest;
            b->size = size;
        }
        b->free = 0;
        return (uint8_t *)b + HDR;
    }
    return NULL;
}

void *kcalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    struct block *b = (struct block *)((uint8_t *)p - HDR);
    if (b->free) return;
    b->free = 1;

    if (b->next && b->next->free) {
        b->size += HDR + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        b->prev->size += HDR + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

void heap_stats(struct heap_stats *s) {
    s->used = s->free = s->blocks = 0;
    for (struct block *b = head; b; b = b->next) {
        s->blocks++;
        if (b->free) s->free += b->size;
        else s->used += b->size;
    }
}
