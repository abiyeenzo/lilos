/* Fuzz hote du systeme de fichiers en RAM, compare a un modele de reference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <setjmp.h>
#include "lilos.h"

void panic(const char *m) { printf("PANIC: %s\n", m); exit(2); }

#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

struct ref { int used; char name[40]; unsigned char data[FS_FILE_MAX]; uint32_t size; };
static struct ref model[64];

static const char *names[] = { "a", "b.txt", "c-d_e", "..", "bad/n", "", "x y",
    "AAAAAAAAAAAAAAAAAAAAAAA", "AAAAAAAAAAAAAAAAAAAAAAAA", "f1", "f2", "f3", "f4", "f5", "f6",
    "f7", "f8", "f9", "g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8", "g9", "h1", "h2", "h3", "h4",
    "h5", "h6", "h7", "h8", "h9", "i1", "i2" };
#define NN (int)(sizeof names / sizeof *names)

static int ok_name(const char *n) {
    size_t l = strlen(n);
    if (!l || l >= FS_NAME_MAX) return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') ||
              *n == '.' || *n == '-' || *n == '_')) return 0;
    return 1;
}
static struct ref *mfind(const char *n) {
    for (int i = 0; i < 64; i++) if (model[i].used && !strcmp(model[i].name, n)) return &model[i];
    return NULL;
}
static int mcount(void) { int c = 0; for (int i = 0; i < 64; i++) c += model[i].used; return c; }

static void compare(void) {
    const char *name; uint32_t size;
    int n = 0;
    while (fs_list(n, &name, &size)) {
        struct ref *m = mfind(name);
        if (!m || m->size != size) FAIL("liste: %s", name);
        const uint8_t *d; uint32_t len;
        if (fs_read(name, &d, &len) || len != size || memcmp(d, m->data, size)) FAIL("contenu de %s", name);
        n++;
    }
    if (n != mcount()) FAIL("nombre de fichiers %d != %d", n, mcount());
}

int main(void) {
    size_t hsize = 8u << 20;
    void *mem = mmap(NULL, hsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    uint32_t lo = (uint32_t)(uintptr_t)mem;
    srand(7);

    for (int round = 0; round < 60; round++) {
        heap_init(lo, lo + hsize);
        memset(model, 0, sizeof model);
        for (int it = 0; it < 4000; it++) {
            const char *n = names[rand() % NN];
            int op = rand() % 5;
            if (op <= 1) {                                   /* write / append */
                int append = op;
                uint32_t len = (rand() % 4 == 0) ? (uint32_t)(rand() % 6000) : (uint32_t)(rand() % 200);
                unsigned char data[6000];
                for (uint32_t i = 0; i < len; i++) data[i] = (unsigned char)rand();
                int r = fs_write(n, data, len, append);
                struct ref *m = mfind(n);
                uint32_t total = append && m ? m->size + len : len;
                int expect = !ok_name(n) ? FS_ENAME : len > FS_FILE_MAX || total > FS_FILE_MAX ? FS_ETOOBIG
                           : (!m && mcount() >= FS_MAX_FILES) ? FS_EFULL : FS_OK;
                if (r != expect) FAIL("write(%s,len=%u,append=%d) = %d, attendu %d", n, len, append, r, expect);
                if (r == FS_OK) {
                    if (!m) { for (int i = 0; i < 64; i++) if (!model[i].used) { m = &model[i]; break; } m->used = 1; strcpy(m->name, n); m->size = 0; }
                    if (!append) m->size = 0;
                    memcpy(m->data + m->size, data, len);
                    m->size += len;
                }
            } else if (op == 2) {                            /* rm */
                int r = fs_remove(n);
                struct ref *m = mfind(n);
                if ((r == FS_OK) != (m != NULL)) FAIL("rm(%s) = %d", n, r);
                if (m) m->used = 0;
            } else if (op == 3) {                            /* cp */
                const char *d = names[rand() % NN];
                struct ref *s = mfind(n);
                int r = fs_copy(n, d);
                if (!s) { if (r != FS_ENOENT) FAIL("cp source absente = %d", r); continue; }
                struct ref *dm = mfind(d);
                int expect = !ok_name(d) ? FS_ENAME : (!dm && mcount() >= FS_MAX_FILES) ? FS_EFULL : FS_OK;
                if (r != expect) FAIL("cp(%s,%s) = %d, attendu %d", n, d, r, expect);
                if (r == FS_OK) {
                    if (!dm) { for (int i = 0; i < 64; i++) if (!model[i].used) { dm = &model[i]; break; } dm->used = 1; strcpy(dm->name, d); }
                    if (dm != s) { dm->size = s->size; memcpy(dm->data, s->data, s->size); }
                }
            } else {
                compare();
            }
        }
        compare();
        for (int i = 0; i < 64; i++) if (model[i].used) { if (fs_remove(model[i].name)) FAIL("rm final"); }
        struct heap_stats s;
        heap_stats(&s);
        if (s.used || s.blocks != 1) FAIL("fuite: used=%u blocks=%u", s.used, s.blocks);
    }
    printf("fs: ok\n");

    /* parse_int : bornes */
    int32_t v;
    if (parse_int("2147483647", &v) || v != 2147483647) FAIL("parse_int max");
    if (parse_int("-2147483648", &v) || v != INT32_MIN) FAIL("parse_int min");
    if (!parse_int("2147483648", &v) || !parse_int("-2147483649", &v) || !parse_int("-", &v) || !parse_int("--1", &v)) FAIL("parse_int bornes");
    printf("parse_int: ok\n");
    return 0;
}
