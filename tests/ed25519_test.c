/* Teste kernel/sha512.c et kernel/ed25519.c contre les vecteurs de gen_ed25519.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lilos.h"

#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

static int hex2bin(const char *h, uint8_t *out, size_t max) {
    size_t n = strlen(h) / 2;
    if (n > max) return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return (int)n;
}

int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "build/ed25519.vec", "r");
    if (!f) FAIL("vecteurs introuvables (python3 tests/gen_ed25519.py > build/ed25519.vec)");
    size_t cap = 1100000 * 2 + 64;
    char *line = malloc(cap);
    uint8_t *msg = malloc(1100000), *dig = malloc(64), pk[32], sig[64];
    int ed_ok = 0, ed_bad = 0, sha = 0;
    double t_total = 0;

    while (fgets(line, (int)cap, f)) {
        char *tok[5];
        int n = 0;
        for (char *p = strtok(line, " \n"); p && n < 5; p = strtok(NULL, " \n")) tok[n++] = p;

        if (!strcmp(tok[0], "sha")) {
            int len = strcmp(tok[1], "-") ? hex2bin(tok[1], msg, 1100000) : 0;
            uint8_t want[64], got[64];
            hex2bin(tok[2], want, 64);
            sha512(msg, (uint32_t)len, got);
            if (memcmp(want, got, 64)) FAIL("sha512 d'un message de %d octets", len);
            /* en flux : decoupage irregulier */
            struct sha512 s;
            sha512_init(&s);
            for (int pos = 0, step = 1; pos < len; pos += step, step = step * 3 % 97 + 1) {
                int take = len - pos < step ? len - pos : step;
                sha512_update(&s, msg + pos, (uint32_t)take);
            }
            sha512_final(&s, got);
            if (memcmp(want, got, 64)) FAIL("sha512 en flux d'un message de %d octets", len);
            sha++;
        } else {
            int len = strcmp(tok[3], "-") ? hex2bin(tok[3], msg, 1100000) : 0;
            hex2bin(tok[1], pk, 32);
            hex2bin(tok[2], sig, 64);
            int want = atoi(tok[4]);
            struct timespec a, b;
            clock_gettime(CLOCK_MONOTONIC, &a);
            int got = ed25519_verify(sig, msg, (uint32_t)len, pk);
            clock_gettime(CLOCK_MONOTONIC, &b);
            t_total += (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
            if (got != want) FAIL("ed25519 : attendu %d, obtenu %d (pk=%.16s... msg de %d octets)", want, got, tok[1], len);
            if (want) ed_ok++; else ed_bad++;
        }
    }
    free(line); free(msg); free(dig); fclose(f);
    printf("sha512 : %d cas ok\n", sha);
    printf("ed25519 : %d signatures valides acceptees, %d invalides refusees\n", ed_ok, ed_bad);
    printf("temps moyen par verification : %.1f ms (hote)\n", 1000 * t_total / (ed_ok + ed_bad));
    return 0;
}
