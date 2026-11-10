/* Fuzz hote du systeme de fichiers (droits compris), compare a un modele. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "lilos.h"

void panic(const char *m) { printf("PANIC: %s\n", m); exit(2); }

#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

struct ref { int used; char name[40]; unsigned char data[FS_FILE_MAX]; uint32_t size, uid; uint8_t mode; };
static struct ref model[64];

static const char *names[] = { "a", "b.txt", "c-d_e", "..", "bad/n", "", "x y",
    "AAAAAAAAAAAAAAAAAAAAAAA", "AAAAAAAAAAAAAAAAAAAAAAAA", "f1", "f2", "f3", "f4", "f5", "f6",
    "f7", "f8", "f9", "g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8", "g9", "h1", "h2", "h3", "h4",
    "h5", "h6", "h7", "h8", "h9", "i1", "i2" };
#define NN (int)(sizeof names / sizeof *names)
static const uint32_t UIDS[] = { 0, 1000, 1001 };

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
static int may(const struct ref *f, uint32_t uid, int want) {
    if (uid == 0) return 1;
    int bits = f->uid == uid ? (f->mode >> 3) & 7 : f->mode & 7;
    return (bits & want) == want;
}
static struct ref *mnew(const char *n, uint32_t uid) {
    for (int i = 0; i < 64; i++) if (!model[i].used) {
        struct ref *m = &model[i];
        m->used = 1; strcpy(m->name, n); m->size = 0; m->uid = uid; m->mode = FS_MODE_DEFAULT;
        return m;
    }
    return NULL;
}

/* resultat attendu de fs_write */
static int expect_write(const char *n, uint32_t len, int append, uint32_t uid) {
    struct ref *m = mfind(n);
    if (!ok_name(n)) return FS_ENAME;
    if (len > FS_FILE_MAX) return FS_ETOOBIG;
    if (m && !may(m, uid, 2)) return FS_EPERM;
    uint32_t total = append && m ? m->size + len : len;
    if (total > FS_FILE_MAX) return FS_ETOOBIG;
    if (!m && mcount() >= FS_MAX_FILES) return FS_EFULL;
    return FS_OK;
}

static void compare(void) {
    struct fs_info f;
    int n = 0;
    while (fs_list(n, &f)) {
        struct ref *m = mfind(f.name);
        if (!m || m->size != f.size || m->uid != f.uid || m->mode != f.mode) FAIL("liste: %s", f.name);
        const uint8_t *d; uint32_t len;
        if (fs_read(f.name, &d, &len, 0) || len != f.size || memcmp(d, m->data, len)) FAIL("contenu de %s", f.name);
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
            uint32_t uid = UIDS[rand() % 3];
            int op = rand() % 8;
            if (op <= 1) {                                   /* write / append */
                int append = op;
                uint32_t len = (rand() % 4 == 0) ? (uint32_t)(rand() % 6000) : (uint32_t)(rand() % 200);
                unsigned char data[6000];
                for (uint32_t i = 0; i < len; i++) data[i] = (unsigned char)rand();
                int expect = expect_write(n, len, append, uid);
                int r = fs_write(n, data, len, append, uid);
                if (r != expect) FAIL("write(%s,len=%u,append=%d,uid=%u) = %d, attendu %d", n, len, append, uid, r, expect);
                if (r == FS_OK) {
                    struct ref *m = mfind(n);
                    if (!m) m = mnew(n, uid);
                    if (!append) m->size = 0;
                    memcpy(m->data + m->size, data, len);
                    m->size += len;
                }
            } else if (op == 2) {                            /* rm */
                int r = fs_remove(n, uid);
                struct ref *m = mfind(n);
                int expect = !m ? FS_ENOENT : (uid != 0 && m->uid != uid) ? FS_EPERM : FS_OK;
                if (r != expect) FAIL("rm(%s,uid=%u) = %d, attendu %d", n, uid, r, expect);
                if (r == FS_OK) m->used = 0;
            } else if (op == 3) {                            /* cp */
                const char *d = names[rand() % NN];
                struct ref *s = mfind(n);
                int expect;
                if (!s) expect = FS_ENOENT;
                else if (!may(s, uid, 4)) expect = FS_EPERM;
                else expect = expect_write(d, s->size, 0, uid);
                int r = fs_copy(n, d, uid);
                if (r != expect) FAIL("cp(%s,%s,uid=%u) = %d, attendu %d", n, d, uid, r, expect);
                if (r == FS_OK) {
                    struct ref *dm = mfind(d);
                    if (!dm) dm = mnew(d, uid);
                    if (dm != s) { dm->size = s->size; memcpy(dm->data, s->data, s->size); }
                }
            } else if (op == 4) {                            /* chmod */
                uint8_t mode = (uint8_t)rand();
                struct ref *m = mfind(n);
                int expect = !m ? FS_ENOENT : (uid != 0 && m->uid != uid) ? FS_EPERM : FS_OK;
                int r = fs_chmod(n, mode, uid);
                if (r != expect) FAIL("chmod(%s,uid=%u) = %d, attendu %d", n, uid, r, expect);
                if (r == FS_OK) m->mode = mode & 077;
            } else if (op == 5) {                            /* chown */
                uint32_t nu = UIDS[rand() % 3];
                struct ref *m = mfind(n);
                int expect = !m ? FS_ENOENT : uid != 0 ? FS_EPERM : FS_OK;
                int r = fs_chown(n, nu, uid);
                if (r != expect) FAIL("chown(%s,uid=%u) = %d, attendu %d", n, uid, r, expect);
                if (r == FS_OK) m->uid = nu;
            } else if (op == 6) {                            /* read avec droits */
                const uint8_t *d; uint32_t len;
                struct ref *m = mfind(n);
                int expect = !m ? FS_ENOENT : !may(m, uid, 4) ? FS_EPERM : FS_OK;
                int r = fs_read(n, &d, &len, uid);
                if (r != expect) FAIL("read(%s,uid=%u) = %d, attendu %d", n, uid, r, expect);
                if (r == FS_OK && (len != m->size || memcmp(d, m->data, len))) FAIL("read contenu");
            } else {
                compare();
            }
        }
        compare();
        for (int i = 0; i < 64; i++) if (model[i].used) { if (fs_remove(model[i].name, 0)) FAIL("rm final"); }
        struct heap_stats s;
        heap_stats(&s);
        if (s.used || s.blocks != 1) FAIL("fuite: used=%u blocks=%u", s.used, s.blocks);
    }
    printf("fs: ok\n");

    /* suppression d'un compte : ses fichiers disparaissent, ceux des autres restent */
    heap_init(lo, lo + hsize);
    fs_write("a", "x", 1, 0, 1000); fs_write("b", "y", 1, 0, 1001); fs_write("c", "z", 1, 0, 1000);
    fs_remove_owned_by(1000);
    struct fs_info f;
    int count = 0;
    while (fs_list((uint32_t)count, &f)) { if (f.uid != 1001) FAIL("fichier d'un autre uid"); count++; }
    if (count != 1) FAIL("remove_owned_by: %d fichiers restants", count);
    printf("fs_remove_owned_by: ok\n");

    int32_t v;
    if (parse_int("2147483647", &v) || v != 2147483647) FAIL("parse_int max");
    if (parse_int("-2147483648", &v) || v != INT32_MIN) FAIL("parse_int min");
    if (!parse_int("2147483648", &v) || !parse_int("-2147483649", &v) || !parse_int("-", &v) || !parse_int("--1", &v)) FAIL("parse_int bornes");
    printf("parse_int: ok\n");
    return 0;
}
