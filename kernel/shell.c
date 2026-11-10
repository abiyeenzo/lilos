#include "lilos.h"

#define LINE_MAX 128
#define ARGV_MAX 16
#define HIST_MAX 16

static char history[HIST_MAX][LINE_MAX];
static uint32_t hist_count;

extern char _end[];

/* --- saisie --- */

static void replace_line(char *buf, int *len, const char *src) {
    while (*len > 0) {
        kputc('\b');
        (*len)--;
    }
    strncpy(buf, src, LINE_MAX - 1);
    buf[LINE_MAX - 1] = 0;
    *len = (int)strlen(buf);
    kputs(buf);
}

static void prompt(void) {
    vga_set_color(10, 0);
    kputs("lilos");
    vga_set_color(7, 0);
    kputs("> ");
}

static void readline(char *buf) {
    int len = 0;
    uint32_t oldest = hist_count > HIST_MAX ? hist_count - HIST_MAX : 0;
    uint32_t pos = hist_count;
    buf[0] = 0;

    for (;;) {
        int k = keyboard_getc();

        if (k == '\n') {
            kputc('\n');
            buf[len] = 0;
            return;
        } else if (k == '\b') {
            if (len > 0) {
                len--;
                kputc('\b');
            }
        } else if (k == KEY_UP) {
            if (pos > oldest) {
                pos--;
                replace_line(buf, &len, history[pos % HIST_MAX]);
            }
        } else if (k == KEY_DOWN) {
            if (pos < hist_count) {
                pos++;
                replace_line(buf, &len, pos == hist_count ? "" : history[pos % HIST_MAX]);
            }
        } else if (k == 12) {               /* Ctrl+L */
            vga_clear();
            prompt();
            buf[len] = 0;
            kputs(buf);
        } else if (k >= 32 && k < 127 && len < LINE_MAX - 1) {
            buf[len++] = (char)k;
            kputc((char)k);
        }
    }
}

/* --- commandes --- */

struct command {
    const char *name;
    const char *help;
    void (*fn)(int argc, char **argv);
};

static const struct command commands[];

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    for (const struct command *c = commands; c->name; c++)
        kprintf("  %-10s %s\n", c->name, c->help);
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) kputc(' ');
        kputs(argv[i]);
    }
    kputc('\n');
}

static void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("LilOS %s (x86, 32 bits)\n", LILOS_VERSION);
}

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t s = timer_ticks() / TIMER_HZ;
    kprintf("%u h %u min %u s (%u ticks)\n", s / 3600, (s / 60) % 60, s % 60,
            timer_ticks());
}

static void cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    struct rtc_time t;
    rtc_read(&t);
    kprintf("%04u-%02u-%02u %02u:%02u:%02u\n", t.year, t.month, t.day,
            t.hour, t.min, t.sec);
}

static void cmd_mem(int argc, char **argv) {
    (void)argc; (void)argv;
    struct heap_stats s;
    heap_stats(&s);
    kprintf("kernel : %p - %p (%u Ko)\n", (void *)0x10000, _end,
            ((uint32_t)_end - 0x10000) / 1024);
    kprintf("heap   : %u Ko utilises, %u Ko libres, %u blocs\n",
            s.used / 1024, s.free / 1024, s.blocks);
    kprintf("paging : identite sur %u Mo\n", RAM_MAP_END / (1024 * 1024));
}

static void cpuid(uint32_t leaf, uint32_t *r) {
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(0));
}

static void cmd_cpuid(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t r[4];
    char vendor[13];

    cpuid(0, r);
    memcpy(vendor, &r[1], 4);
    memcpy(vendor + 4, &r[3], 4);
    memcpy(vendor + 8, &r[2], 4);
    vendor[12] = 0;
    kprintf("vendeur : %s\n", vendor);

    cpuid(0x80000000, r);
    if (r[0] >= 0x80000004) {
        char brand[49];
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, r);
            memcpy(brand + i * 16, r, 16);
        }
        brand[48] = 0;
        const char *b = brand;
        while (*b == ' ') b++;
        kprintf("modele  : %s\n", b);
    }
}

static void cmd_color(int argc, char **argv) {
    uint32_t fg, bg = 0;
    if (argc < 2 || parse_uint(argv[1], &fg) || fg > 15 ||
        (argc > 2 && (parse_uint(argv[2], &bg) || bg > 15))) {
        kputs("usage: color <texte 0-15> [fond 0-15]\n");
        return;
    }
    vga_set_color((uint8_t)fg, (uint8_t)bg);
}

static void cmd_sleep(int argc, char **argv) {
    uint32_t ms;
    if (argc < 2 || parse_uint(argv[1], &ms) || ms > 600000) {
        kputs("usage: sleep <ms>\n");
        return;
    }
    timer_sleep_ms(ms);
}

static void cmd_hexdump(int argc, char **argv) {
    uint32_t addr, len = 64;
    if (argc < 2 || parse_uint(argv[1], &addr) ||
        (argc > 2 && parse_uint(argv[2], &len)) || len == 0 || len > 512) {
        kputs("usage: hexdump <adresse> [longueur <= 512]\n");
        return;
    }
    if (addr < 0x1000 || addr > RAM_MAP_END || len > RAM_MAP_END - addr) {
        kputs("adresse hors de la zone mappee (0x1000 - 0x1000000)\n");
        return;
    }
    const uint8_t *p = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i += 16) {
        kprintf("%08x  ", addr + i);
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) kprintf("%02x ", p[i + j]);
            else kputs("   ");
        }
        kputc(' ');
        for (uint32_t j = 0; j < 16 && i + j < len; j++) {
            char c = (char)p[i + j];
            kputc(c >= 32 && c < 127 ? c : '.');
        }
        kputc('\n');
    }
}

static void cmd_memtest(int argc, char **argv) {
    (void)argc; (void)argv;
    void *p[32];
    int ok = 1;

    for (int i = 0; i < 32; i++) {
        p[i] = kmalloc(64 + i * 32);
        if (!p[i]) { ok = 0; break; }
        memset(p[i], i, 64 + i * 32);
    }
    for (int i = 0; ok && i < 32; i++) {
        uint8_t *b = p[i];
        for (int j = 0; j < 64 + i * 32; j++)
            if (b[j] != i) { ok = 0; break; }
    }
    for (int i = 0; i < 32; i += 2) kfree(p[i]);
    for (int i = 1; i < 32; i += 2) kfree(p[i]);

    struct heap_stats s;
    heap_stats(&s);
    if (s.used != 0 || s.blocks != 1) ok = 0;
    kprintf("memtest : %s\n", ok ? "OK" : "ECHEC");
}

static void cmd_history(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t first = hist_count > HIST_MAX ? hist_count - HIST_MAX : 0;
    for (uint32_t i = first; i < hist_count; i++)
        kprintf("%3u  %s\n", i + 1, history[i % HIST_MAX]);
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    kputs("redemarrage...\n");
    __asm__ volatile ("cli");
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    kputs("arret du systeme.\n");
    __asm__ volatile ("cli");
    outw(0x604, 0x2000);        /* QEMU */
    outw(0xB004, 0x2000);       /* Bochs, anciens QEMU */
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_panic(int argc, char **argv) {
    (void)argc; (void)argv;
    panic("declenche depuis le shell");
}

static const struct command commands[] = {
    { "help",    "liste les commandes",                  cmd_help },
    { "clear",   "efface l'ecran (Ctrl+L)",              cmd_clear },
    { "echo",    "affiche ses arguments",                cmd_echo },
    { "version", "version du systeme",                   cmd_version },
    { "uptime",  "duree depuis le demarrage",            cmd_uptime },
    { "date",    "date et heure (horloge CMOS)",         cmd_date },
    { "mem",     "memoire kernel, heap et paging",       cmd_mem },
    { "cpuid",   "vendeur et modele du processeur",      cmd_cpuid },
    { "color",   "couleurs du texte : color <fg> [bg]",  cmd_color },
    { "sleep",   "pause : sleep <ms>",                   cmd_sleep },
    { "hexdump", "dump memoire : hexdump <adr> [len]",   cmd_hexdump },
    { "memtest", "test de l'allocateur",                 cmd_memtest },
    { "history", "dernieres commandes",                  cmd_history },
    { "reboot",  "redemarre",                            cmd_reboot },
    { "halt",    "eteint (QEMU) ou arrete le CPU",       cmd_halt },
    { "panic",   "provoque un kernel panic",             cmd_panic },
    { NULL, NULL, NULL }
};

static void execute(char *line) {
    char *argv[ARGV_MAX];
    int argc = 0;

    for (char *p = line; *p;) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (argc < ARGV_MAX) argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (!argc) return;

    for (const struct command *c = commands; c->name; c++) {
        if (!strcmp(c->name, argv[0])) {
            c->fn(argc, argv);
            return;
        }
    }
    kprintf("commande inconnue: %s (essayez help)\n", argv[0]);
}

void shell_run(void) {
    char line[LINE_MAX];

    for (;;) {
        prompt();
        readline(line);
        if (!line[0]) continue;

        strncpy(history[hist_count % HIST_MAX], line, LINE_MAX - 1);
        hist_count++;
        execute(line);
    }
}
