#include "lilos.h"

/* Programmes utilisateur embarques dans l'image du noyau (objcopy -I binary). */

struct program { const char *name; const uint8_t *start, *end; };

#define PROG(n) \
    extern const uint8_t _binary_build_user_##n##_bin_start[], _binary_build_user_##n##_bin_end[];
#define ENTRY(n) { #n, _binary_build_user_##n##_bin_start, _binary_build_user_##n##_bin_end }

PROG(bonjour) PROG(moi) PROG(lire) PROG(noter) PROG(noyau) PROG(ecrase) PROG(boucle) PROG(saisie) PROG(attaque)

const struct program programs[] = {
    ENTRY(bonjour), ENTRY(moi), ENTRY(lire), ENTRY(noter),
    ENTRY(noyau), ENTRY(ecrase), ENTRY(boucle), ENTRY(saisie), ENTRY(attaque),
};
const int program_count = (int)(sizeof(programs) / sizeof(programs[0]));
