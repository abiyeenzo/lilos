/* Vecteurs de test SHA-256 (FIPS 180-4) et comparaison avec une implementation de reference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lilos.h"

static void hex(const uint8_t *h, char *out) { for (int i = 0; i < 32; i++) sprintf(out + 2*i, "%02x", h[i]); }
#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

static void check(const char *in, uint32_t len, const char *want) {
    uint8_t h[32]; char s[65];
    sha256(in, len, h); hex(h, s);
    if (strcmp(s, want)) FAIL("sha256(%.20s..., %u) = %s", in, len, s);
}

int main(void) {
    check("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    char *m = malloc(1000000);
    memset(m, 'a', 1000000);
    check(m, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    free(m);
    /* frontieres du bourrage : 55, 56, 63, 64, 65 octets */
    unsigned char buf[200];
    for (int i = 0; i < 200; i++) buf[i] = (unsigned char)i;
    FILE *p = popen("python3 -c \"import hashlib;b=bytes(range(200));[print(hashlib.sha256(b[:n]).hexdigest()) for n in range(0,200)]\"", "r");
    if (p) {
        char line[128];
        for (int n = 0; n < 200 && fgets(line, sizeof line, p); n++) {
            line[64] = 0;
            check((const char *)buf, (uint32_t)n, line);
        }
        pclose(p);
    }
    printf("sha256: ok\n");
    return 0;
}
