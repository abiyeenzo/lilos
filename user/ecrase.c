#include "ulib.h"

/* Tente d'ecrire dans son propre code (page en lecture seule). */
int main(void) {
    puts_("ecriture dans mon propre code...\n");
    *(volatile char *)0x40000100 = 0;
    puts_("BREACH : ecriture reussie\n");
    return 0;
}
