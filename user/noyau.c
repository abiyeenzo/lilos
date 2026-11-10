#include "ulib.h"

/* Tente de lire la memoire du noyau : le processus doit etre tue. */
int main(void) {
    volatile unsigned *kernel = (volatile unsigned *)0x10000;
    puts_("lecture de la memoire du noyau...\n");
    putnum((int)*kernel);
    puts_("\nBREACH : lecture reussie\n");
    return 0;
}
