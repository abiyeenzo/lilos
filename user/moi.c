#include "ulib.h"

int main(int argc, char **argv) {
    puts_("uid="); putnum(getuid_());
    puts_(" pid="); putnum(getpid_());
    puts_(" arguments="); putnum(argc - 1);
    for (int i = 1; i < argc; i++) { puts_(" ["); puts_(argv[i]); puts_("]"); }
    puts_("\n");
    return 0;
}
