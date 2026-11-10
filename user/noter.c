#include "ulib.h"

int main(int argc, char **argv) {
    if (argc < 3) { puts_("usage : lancer noter <fichier> <texte>\n"); return 1; }
    int n = sys(SYS_FWRITE, (int)argv[1], (int)argv[2], slen(argv[2]), 0);
    if (n < 0) { puts_("noter : erreur "); putnum(n); puts_("\n"); return 1; }
    puts_("écrit\n");
    return 0;
}
