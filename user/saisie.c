#include "ulib.h"

static char line[128];

int main(void) {
    puts_("Votre nom ? ");
    int n = sys(SYS_READ, 0, (int)line, sizeof(line), 0);
    if (n < 0) return 1;
    puts_("Bonjour, "); puts_(line); puts_(" !\n");
    return 0;
}
