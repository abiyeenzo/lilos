#include "ulib.h"

static char buf[4096];

int main(int argc, char **argv) {
    if (argc < 2) { puts_("usage : lancer lire <fichier>\n"); return 1; }
    int n = sys(SYS_FREAD, (int)argv[1], (int)buf, sizeof(buf), 0);
    if (n < 0) { puts_("lire : erreur "); putnum(n); puts_("\n"); return 1; }
    sys(SYS_WRITE, 1, (int)buf, n, 0);
    return 0;
}
