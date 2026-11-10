#include "ulib.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts_("Bonjour depuis le mode utilisateur (ring 3) !\n");
    return 0;
}
