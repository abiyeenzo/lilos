#include "lilos.h"

#define LINE_MAX 128
#define ARGV_MAX 16
#define HIST_MAX 16

static char history[HIST_MAX][LINE_MAX];
static uint32_t hist_count;

extern char _end[];

/* --- saisie --- */

/* Une ligne est une chaine UTF-8 ; cl[] garde la taille en octets de chaque
 * caractere pour que Retour arriere efface un caractere entier. */
struct line {
    char    buf[LINE_MAX];
    uint8_t cl[LINE_MAX];
    int     len, nch;
};

static int utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static void line_clear(struct line *l) {
    while (l->nch > 0) {
        kputc('\b');
        l->nch--;
    }
    l->len = 0;
    l->buf[0] = 0;
}

static void line_set(struct line *l, const char *src) {
    line_clear(l);
    for (; *src && l->len < LINE_MAX - 1; src++) {
        l->buf[l->len++] = *src;
        if (((uint8_t)*src & 0xC0) != 0x80) {
            l->cl[l->nch++] = 1;
        } else if (l->nch) {
            l->cl[l->nch - 1]++;
        }
    }
    l->buf[l->len] = 0;
    kputs(l->buf);
}

static void line_add(struct line *l, uint32_t cp) {
    char enc[4];
    int n = utf8_encode(cp, enc);
    if (l->len + n >= LINE_MAX) return;
    memcpy(l->buf + l->len, enc, (size_t)n);
    l->len += n;
    l->cl[l->nch++] = (uint8_t)n;
    l->buf[l->len] = 0;
    for (int i = 0; i < n; i++) kputc(enc[i]);
}

static void line_back(struct line *l) {
    if (!l->nch) return;
    l->len -= l->cl[--l->nch];
    l->buf[l->len] = 0;
    kputc('\b');
}

static void prompt(void) {
    vga_set_color(10, 0);
    kputs("lilos");
    vga_set_color(7, 0);
    kputs("> ");
}

static void readline(char *out) {
    struct line l = { .len = 0, .nch = 0 };
    uint32_t oldest = hist_count > HIST_MAX ? hist_count - HIST_MAX : 0;
    uint32_t pos = hist_count;
    l.buf[0] = 0;

    for (;;) {
        int k = keyboard_getc();

        if (k == '\n') {
            kputc('\n');
            memcpy(out, l.buf, (size_t)l.len + 1);
            return;
        } else if (k == '\b') {
            line_back(&l);
        } else if (k == KEY_UP) {
            if (pos > oldest) {
                pos--;
                line_set(&l, history[pos % HIST_MAX]);
            }
        } else if (k == KEY_DOWN) {
            if (pos < hist_count) {
                pos++;
                line_set(&l, pos == hist_count ? "" : history[pos % HIST_MAX]);
            }
        } else if (k == 12) {               /* Ctrl+L */
            vga_clear();
            prompt();
            kputs(l.buf);
        } else if (k >= 32 && k != 127 && k < 0x110000) {
            line_add(&l, (uint32_t)k);
        }
    }
}

/* --- commandes --- */

struct command {
    const char *name;
    const char *alias;      /* ancien nom anglais, toujours accepte */
    const char *help;
    void (*fn)(int argc, char **argv);
};

static const struct command commands[];

static void cmd_help(int argc, char **argv) {
    if (argc > 1) {
        for (const struct command *c = commands; c->name; c++) {
            if (!strcmp(c->name, argv[1]) || (c->alias && !strcmp(c->alias, argv[1]))) {
                kprintf("%s : %s\n", c->name, c->help);
                return;
            }
        }
        kprintf("pas d'aide pour %s\n", argv[1]);
        return;
    }
    int col = 0;
    for (const struct command *c = commands; c->name; c++) {
        int l = (int)strlen(c->name) + 2;
        if (col + l > 78) { kputc('\n'); col = 0; }
        kprintf("%s  ", c->name);
        col += l;
    }
    kputs("\n\naide <commande> pour le détail. Flèches : historique. Ctrl+L : efface.\n");
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
    kprintf("heap   : %u Ko utilisés, %u Ko libres, %u blocs\n",
            s.used / 1024, s.free / 1024, s.blocks);
    kprintf("paging : PAE, identité sur %u Mo, NX %s, W^X actif\n",
            RAM_MAP_END / (1024 * 1024), paging_nx_enabled() ? "actif" : "indisponible");
}

#define cpuid cpuid_leaf

static void cmd_cpuid(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t r[4];
    char vendor[13];

    cpuid(0, r);
    memcpy(vendor, &r[1], 4);
    memcpy(vendor + 4, &r[3], 4);
    memcpy(vendor + 8, &r[2], 4);
    vendor[12] = 0;
    kprintf("fabricant : %s\n", vendor);

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
        kprintf("modèle    : %s\n", b);
    }
}

static void cmd_color(int argc, char **argv) {
    uint32_t fg, bg = 0;
    if (argc < 2 || parse_uint(argv[1], &fg) || fg > 15 ||
        (argc > 2 && (parse_uint(argv[2], &bg) || bg > 15))) {
        kputs("usage : couleur <texte 0-15> [fond 0-15]\n");
        return;
    }
    vga_set_color((uint8_t)fg, (uint8_t)bg);
}

static void cmd_sleep(int argc, char **argv) {
    uint32_t ms;
    if (argc < 2 || parse_uint(argv[1], &ms) || ms > 600000) {
        kputs("usage : pause <ms>\n");
        return;
    }
    timer_sleep_ms(ms);
}

static void cmd_hexdump(int argc, char **argv) {
    uint32_t addr, len = 64;
    if (argc < 2 || parse_uint(argv[1], &addr) ||
        (argc > 2 && parse_uint(argv[2], &len)) || len == 0 || len > 512) {
        kputs("usage : hexdump <adresse> [longueur <= 512]\n");
        return;
    }
    if (!paging_range_mapped(addr, len)) {
        kputs("adresse hors de la zone mappée\n");
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
    void *p[32] = { NULL };
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
    if (kmalloc(0xFFFFFFFFu) || kmalloc(HEAP_END) || kmalloc(0)) ok = 0;
    for (int i = 0; i < 32; i += 2) kfree(p[i]);
    for (int i = 1; i < 32; i += 2) kfree(p[i]);

    struct heap_stats s;
    heap_stats(&s);
    if (s.used != 0 || s.blocks != 1) ok = 0;
    kprintf("memtest : %s\n", ok ? "OK" : "ÉCHEC");
}

static void cmd_history(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t first = hist_count > HIST_MAX ? hist_count - HIST_MAX : 0;
    for (uint32_t i = first; i < hist_count; i++)
        kprintf("%3u  %s\n", i + 1, history[i % HIST_MAX]);
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    kputs("redémarrage...\n");
    __asm__ volatile ("cli");
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    kputs("arrêt du système.\n");
    __asm__ volatile ("cli");
    outw(0x604, 0x2000);        /* QEMU */
    outw(0xB004, 0x2000);       /* Bochs, anciens QEMU */
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_panic(int argc, char **argv) {
    (void)argc; (void)argv;
    panic("déclenché depuis le shell");
}

/* --- fichiers en RAM et calculatrice --- */

static int join_args(int argc, char **argv, int from, char *buf, size_t max) {
    size_t n = 0;
    for (int i = from; i < argc; i++) {
        size_t l = strlen(argv[i]);
        if (n + l + 2 > max) return -1;
        if (i > from) buf[n++] = ' ';
        memcpy(buf + n, argv[i], l);
        n += l;
    }
    return (int)n;
}

static void fs_report(int r) {
    if (r) kprintf("erreur : %s\n", fs_strerror(r));
}

static void do_write(int argc, char **argv, int append) {
    char buf[LINE_MAX + 2];
    if (argc < 2) {
        kprintf("usage : %s <fichier> [texte]\n", argv[0]);
        return;
    }
    int n = join_args(argc, argv, 2, buf, sizeof(buf) - 1);
    if (n < 0) {
        kputs("erreur : texte trop long\n");
        return;
    }
    if (n > 0) buf[n++] = '\n';
    fs_report(fs_write(argv[1], buf, (uint32_t)n, append));
}

static void cmd_write(int argc, char **argv) { do_write(argc, argv, 0); }
static void cmd_append(int argc, char **argv) { do_write(argc, argv, 1); }

static void cmd_cat(int argc, char **argv) {
    const uint8_t *d;
    uint32_t len;
    if (argc < 2) { kputs("usage : afficher <fichier>\n"); return; }
    int r = fs_read(argv[1], &d, &len);
    if (r) { fs_report(r); return; }
    for (uint32_t i = 0; i < len; i++)
        kputc(d[i] == '\n' || d[i] == '\t' || (d[i] >= 32 && d[i] != 127) ? (char)d[i] : '.');
    if (len && d[len - 1] != '\n') kputc('\n');
}

static void cmd_ls(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *name;
    uint32_t size, total = 0, count = 0;
    while (fs_list(count, &name, &size)) {
        kprintf("  %-24s %5u octets\n", name, size);
        total += size;
        count++;
    }
    kprintf("%u fichier(s), %u octets (max %d fichiers)\n", count, total, FS_MAX_FILES);
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { kputs("usage : supprimer <fichier>\n"); return; }
    fs_report(fs_remove(argv[1]));
}

static void cmd_cp(int argc, char **argv) {
    if (argc < 3) { kputs("usage : copier <source> <destination>\n"); return; }
    fs_report(fs_copy(argv[1], argv[2]));
}

static void cmd_calc(int argc, char **argv) {
    int32_t a, b, r = 0;
    if (argc != 4 || parse_int(argv[1], &a) || parse_int(argv[3], &b) || argv[2][1]) {
        kputs("usage : calculer <a> <+ - * / %> <b>   (entiers 32 bits signés)\n");
        return;
    }
    int bad = 0;
    switch (argv[2][0]) {
    case '+': bad = __builtin_add_overflow(a, b, &r); break;
    case '-': bad = __builtin_sub_overflow(a, b, &r); break;
    case '*': bad = __builtin_mul_overflow(a, b, &r); break;
    case '/':
    case '%':
        if (b == 0) { kputs("erreur : division par zéro\n"); return; }
        if (a == INT32_MIN && b == -1) bad = 1;
        else r = argv[2][0] == '/' ? a / b : a % b;
        break;
    default:
        kputs("opérateur inconnu\n");
        return;
    }
    if (bad) kputs("erreur : débordement (résultat hors 32 bits)\n");
    else kprintf("%d\n", r);
}

#pragma GCC diagnostic ignored "-Winfinite-recursion"
static int recurse(int n) {
    volatile char pad[8192];   /* plus grand qu'une page : teste stack-clash */
    pad[0] = (char)n;
    return recurse(n + 1) + pad[0];
}

static void cmd_stacktest(int argc, char **argv) {
    (void)argc; (void)argv;
    kputs("débordement de pile volontaire...\n");
    kprintf("%d\n", recurse(0));
}

static void cmd_nulltest(int argc, char **argv) {
    (void)argc; (void)argv;
    kputs("lecture à l'adresse 0...\n");
    kprintf("%u\n", *(volatile uint32_t *)0);
}

extern char __rodata_start[];
static const char ro_probe[] = "lecture seule";
static uint8_t data_probe[16] = { 0xC3 };

static void run_code_at(void *addr) {
    *(volatile uint8_t *)addr = 0xC3;           /* ret */
    __asm__ volatile ("call *%0" : : "r"(addr) : "memory", "cc");
}

static void cmd_protect(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "";

    if (!strcmp(what, "text")) {
        kputs("écriture dans le code du kernel...\n");
        *(volatile uint8_t *)(void *)cmd_protect = 0x90;
    } else if (!strcmp(what, "rodata")) {
        kputs("écriture dans rodata...\n");
        *(volatile char *)ro_probe = 'X';
    } else if (!strcmp(what, "stack") || !strcmp(what, "heap") || !strcmp(what, "data")) {
        if (!paging_nx_enabled()) {
            kputs("NX indisponible sur ce processeur\n");
            return;
        }
        kprintf("exécution depuis %s...\n", what);
        if (!strcmp(what, "stack")) {
            uint8_t buf[16];
            run_code_at(buf);
        } else if (!strcmp(what, "heap")) {
            void *p = kmalloc(16);
            run_code_at(p);
        } else {
            run_code_at(data_probe);
        }
        kputs("exécution réussie (pas de protection NX)\n");
    } else {
        kputs("usage : proteger <text|rodata|stack|heap|data>\n");
    }
}

static const struct command commands[] = {
    { "aide", "help", "liste les commandes (aide <commande> pour le détail)", cmd_help },
    { "effacer", "clear", "efface l'écran (Ctrl+L)", cmd_clear },
    { "echo", NULL, "affiche ses arguments", cmd_echo },
    { "version", NULL, "version du système", cmd_version },
    { "temps", "uptime", "durée depuis le démarrage", cmd_uptime },
    { "date", NULL, "date et heure (horloge CMOS)", cmd_date },
    { "memoire", "mem", "mémoire du kernel, heap et paging", cmd_mem },
    { "cpuid", NULL, "fabricant et modèle du processeur", cmd_cpuid },
    { "couleur", "color", "couleurs du texte : couleur <texte> [fond]", cmd_color },
    { "pause", "sleep", "attend un délai : pause <ms>", cmd_sleep },
    { "hexdump", NULL, "affiche la mémoire : hexdump <adresse> [longueur]", cmd_hexdump },
    { "memtest", NULL, "teste l'allocateur mémoire", cmd_memtest },
    { "historique", "history", "dernières commandes", cmd_history },
    { "redemarrer", "reboot", "redémarre la machine", cmd_reboot },
    { "eteindre", "halt", "éteint (QEMU) ou arrête le processeur", cmd_halt },
    { "panic", NULL, "provoque un kernel panic", cmd_panic },
    { "proteger", "protect", "teste W^X : proteger <text|rodata|stack|heap|data>", cmd_protect },
    { "ls", "lister", "liste les fichiers (RAM)", cmd_ls },
    { "ecrire", "write", "écrit un fichier : ecrire <fichier> [texte]", cmd_write },
    { "ajouter", "append", "ajoute une ligne : ajouter <fichier> <texte>", cmd_append },
    { "afficher", "cat", "affiche un fichier", cmd_cat },
    { "copier", "cp", "copie un fichier", cmd_cp },
    { "supprimer", "rm", "supprime un fichier", cmd_rm },
    { "calculer", "calc", "calcule : calculer <a> <+ - * / %> <b>", cmd_calc },
    { "nulltest", NULL, "lit l'adresse 0 (page fault attendu)", cmd_nulltest },
    { "stacktest", NULL, "déborde la pile (double fault attendu)", cmd_stacktest },
    { NULL, NULL, NULL, NULL }
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
        if (!strcmp(c->name, argv[0]) || (c->alias && !strcmp(c->alias, argv[0]))) {
            c->fn(argc, argv);
            return;
        }
    }
    kprintf("commande inconnue : %s (essayez aide)\n", argv[0]);
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
