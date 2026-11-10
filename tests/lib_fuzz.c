/* Fuzz hote des fonctions de base du kernel, compare a la libc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

/* noms du kernel renommes pour ne pas entrer en conflit avec la libc */
#define strlen k_strlen
#define strcmp k_strcmp
#define strncmp k_strncmp
#define strncpy k_strncpy
#define memset k_memset
#define memcpy k_memcpy
#define memmove k_memmove
#define memcmp k_memcmp
#include "../kernel/string.c"
#undef strlen
#undef strcmp
#undef strncmp
#undef strncpy
#undef memset
#undef memcpy
#undef memmove
#undef memcmp

static char out[1 << 16];
static size_t outn;
void vga_putc(char c) { if (outn < sizeof out - 1) out[outn++] = c; }
void serial_putc(char c) { (void)c; }
#include "../kernel/kprintf.c"

#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)
static int sgn(int x) { return (x > 0) - (x < 0); }

int main(void) {
    srand(42);

    /* parse_uint contre strtoull */
    const char *alpha = "0123456789abcdefABCDEFxX -+g";
    for (int i = 0; i < 300000; i++) {
        char s[24];
        int n = rand() % 14;
        for (int j = 0; j < n; j++) s[j] = alpha[rand() % strlen(alpha)];
        s[n] = 0;
        uint32_t v = 0;
        int r = parse_uint(s, &v);
        /* reference : decimal ou 0x..., chiffres uniquement, tient sur 32 bits */
        int ok = n > 0;
        const char *p = s; int base = 10;
        if (ok && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; if (!*p) ok = 0; }
        for (const char *q = p; ok && *q; q++)
            if (!((*q >= '0' && *q <= '9') || (base == 16 && ((*q | 32) >= 'a' && (*q | 32) <= 'f')))) ok = 0;
        unsigned long long ref = 0;
        if (ok) { errno = 0; ref = strtoull(p, NULL, base); if (errno || ref > 0xFFFFFFFFull) ok = 0; }
        if (ok && (r != 0 || v != ref)) FAIL("parse_uint(%s) = %d/%u, attendu %llu", s, r, v, ref);
        if (!ok && r == 0) FAIL("parse_uint(%s) accepte a tort (%u)", s, v);
    }
    printf("parse_uint: ok\n");

    /* memmove avec chevauchement, memcpy, memset, strncpy, strcmp, strncmp */
    for (int i = 0; i < 100000; i++) {
        unsigned char a[300], b[300];
        for (int j = 0; j < 300; j++) a[j] = b[j] = (unsigned char)rand();
        size_t d = rand() % 100, s2 = rand() % 100, n = rand() % 150;
        k_memmove(a + d, a + s2, n);
        memmove(b + d, b + s2, n);
        if (memcmp(a, b, 300)) FAIL("memmove d=%zu s=%zu n=%zu", d, s2, n);
        k_memset(a + d, i, n); memset(b + d, i, n);
        if (memcmp(a, b, 300)) FAIL("memset");
        k_memcpy(a + 150, a, 100); memcpy(b + 150, b, 100);
        if (memcmp(a, b, 300)) FAIL("memcpy");

        char x[40], y[40], z[40], w[40];
        for (int j = 0; j < 39; j++) { x[j] = "ab"[rand() % 2]; y[j] = "ab"[rand() % 2]; }
        x[rand() % 39] = 0; y[rand() % 39] = 0; x[39] = y[39] = 0;
        size_t m = rand() % 45;
        if (sgn(k_strcmp(x, y)) != sgn(strcmp(x, y))) FAIL("strcmp");
        if (sgn(k_strncmp(x, y, m)) != sgn(strncmp(x, y, m))) FAIL("strncmp n=%zu", m);
        if (k_strlen(x) != strlen(x)) FAIL("strlen");
        memset(z, 'Z', sizeof z); memset(w, 'Z', sizeof w);
        k_strncpy(z, x, m < 40 ? m : 40); strncpy(w, x, m < 40 ? m : 40);
        if (memcmp(z, w, 40)) FAIL("strncpy");
    }
    printf("string: ok\n");

    /* kprintf contre snprintf */
    for (int i = 0; i < 200000; i++) {
        char fmt[32], ref[256];
        int32_t sv = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
        uint32_t uv = (uint32_t)sv;
        if (i % 7 == 0) { sv = INT32_MIN; uv = 0; }
        if (i % 11 == 0) { sv = 0; uv = 0xFFFFFFFF; }
        int w = rand() % 14;
        const char *flag = (const char *[]){ "", "0", "-" }[rand() % 3];
        const char *conv = (const char *[]){ "d", "u", "x", "X" }[rand() % 4];
        snprintf(fmt, sizeof fmt, "%%%s%d%s", flag, w, conv);
        outn = 0;
        if (conv[0] == 'd') { kprintf(fmt, sv); snprintf(ref, sizeof ref, fmt, sv); }
        else { kprintf(fmt, uv); snprintf(ref, sizeof ref, fmt, uv); }
        out[outn] = 0;
        if (strcmp(out, ref)) FAIL("kprintf(\"%s\") = [%s], attendu [%s]", fmt, out, ref);
    }
    outn = 0; kprintf("%s|%5s|%-5s|%c|%%|%s", "ab", "cd", "ef", 'g', (char *)0); out[outn] = 0;
    if (strcmp(out, "ab|   cd|ef   |g|%|(null)")) FAIL("kprintf chaines: [%s]", out);
    outn = 0; kprintf("%q%"); out[outn] = 0;
    if (strcmp(out, "%q")) FAIL("kprintf format invalide: [%s]", out);
    printf("kprintf: ok\n");
    return 0;
}
