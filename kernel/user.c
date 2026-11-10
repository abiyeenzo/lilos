#include "lilos.h"

/* Processus en mode utilisateur (ring 3).
 *
 * Un seul processus a la fois (pas d'ordonnanceur) : le shell le lance et
 * attend sa fin. Chaque processus a son propre espace d'adressage PAE :
 * le noyau (identite 0-32 Mo, jamais accessible en ring 3) et une fenetre
 * utilisateur de 1 Mo a 0x40000000 faite de pages tirees d'un pool.
 *   code (lecture + execution), donnees et pile (ecriture, non executable) */

#define P_PRESENT 1ull
#define P_RW      2ull
#define P_USER    4ull
#define P_NX      (1ull << 63)

#define UHDR_MAGIC     0x474F5250u
#define IMAGE_MAX      (64u * 4096)
#define STACK_PAGES    16u
#define ARG_MAX        8
#define ARG_LEN        64
#define MAX_FRAMES     (3 + 64 + STACK_PAGES)

struct uhdr { uint32_t magic, entry, text_end, data_end, bss_end; };

struct program { const char *name; const uint8_t *start, *end; };
extern const struct program programs[];
extern const int program_count;

extern uint32_t user_enter(uint32_t eip, uint32_t esp);
extern void user_return(int status);

/* --- pool de pages physiques (16-32 Mo) --- */

static uint8_t frame_map[(RAM_MAP_END - FRAME_POOL_START) / 4096 / 8];

static uint32_t frame_alloc(void) {
    for (uint32_t i = 0; i < sizeof(frame_map) * 8; i++) {
        if (!(frame_map[i / 8] & (1u << (i % 8)))) {
            frame_map[i / 8] |= (uint8_t)(1u << (i % 8));
            uint32_t phys = FRAME_POOL_START + i * 4096;
            memset((void *)phys, 0, 4096);       /* jamais de fuite entre processus */
            return phys;
        }
    }
    return 0;
}

static void frame_free(uint32_t phys) {
    uint32_t i = (phys - FRAME_POOL_START) / 4096;
    frame_map[i / 8] &= (uint8_t)~(1u << (i % 8));
}

/* --- etat du processus courant --- */

static struct {
    int      active;
    uint32_t pid, uid;
    char     name[16];
    uint64_t *pt;
    uint32_t frames[MAX_FRAMES];
    int      nframes;
} cur;

static uint32_t next_pid = 1;
static volatile int ctrlc;

int user_running(void) { return cur.active; }
void user_ctrlc(void) { if (cur.active) ctrlc = 1; }

int user_kill_pending(void) {
    if (ctrlc && cur.active) {
        ctrlc = 0;
        return 1;
    }
    return 0;
}

void user_abort(int status) {
    user_return(status);
    for (;;) ;
}

int user_program_count(void) { return program_count; }
const char *user_program_name(int i) { return programs[i].name; }

/* --- validation des pointeurs utilisateur --- */

static int uptr_ok(uint32_t addr, uint32_t len, int write) {
    if (!cur.active) return 0;
    if (len == 0) return 1;
    uint32_t last = addr + len - 1;
    if (addr < USER_BASE || last < addr || last >= USER_END) return 0;
    for (uint32_t pg = (addr - USER_BASE) >> 12; pg <= (last - USER_BASE) >> 12; pg++) {
        uint64_t e = cur.pt[pg];
        if (!(e & P_PRESENT) || !(e & P_USER)) return 0;
        if (write && !(e & P_RW)) return 0;
    }
    return 1;
}

static int copy_user_string(uint32_t addr, char *out, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) {
        if (!uptr_ok(addr + i, 1, 0)) return -1;
        out[i] = *(const char *)(addr + i);
        if (!out[i]) return 0;
    }
    return -1;                                   /* pas de NUL : refuse */
}

/* --- chargement --- */

static const struct program *find_program(const char *name) {
    for (int i = 0; i < program_count; i++)
        if (!strcmp(programs[i].name, name)) return &programs[i];
    return NULL;
}

static uint32_t take(uint32_t *list, int *n) {
    uint32_t f = frame_alloc();
    if (f) list[(*n)++] = f;
    return f;
}

static void release_all(void) {
    for (int i = 0; i < cur.nframes; i++) frame_free(cur.frames[i]);
    cur.nframes = 0;
}

int user_run(const char *prog, int argc, char **argv, uint32_t uid) {
    if (cur.active) return -1;
    const struct program *p = find_program(prog);
    if (!p) return -1;

    const struct uhdr *h = (const struct uhdr *)p->start;
    uint32_t size = (uint32_t)(p->end - p->start);
    if (size < sizeof(*h) || h->magic != UHDR_MAGIC || h->data_end > size ||
        h->text_end > h->data_end || h->bss_end < h->data_end || h->bss_end > IMAGE_MAX ||
        (h->text_end & 0xFFF) || h->entry < sizeof(*h) || h->entry >= h->text_end)
        return -1;
    if (argc > ARG_MAX) return -1;
    for (int i = 0; i < argc; i++)
        if (strlen(argv[i]) >= ARG_LEN) return -1;

    memset(&cur, 0, sizeof(cur));
    uint32_t pdpt = take(cur.frames, &cur.nframes);
    uint32_t pd = take(cur.frames, &cur.nframes);
    uint32_t pt = take(cur.frames, &cur.nframes);
    if (!pdpt || !pd || !pt) { release_all(); return -1; }

    uint64_t nx = paging_nx_on() ? P_NX : 0;
    uint64_t *ptab = (uint64_t *)pt;
    uint32_t npages = (h->bss_end + 4095) / 4096;
    uint32_t text_pages = h->text_end / 4096;
    uint32_t stack_top_frame = 0;

    for (uint32_t i = 0; i < npages; i++) {
        uint32_t f = take(cur.frames, &cur.nframes);
        if (!f) { release_all(); return -1; }
        uint32_t off = i * 4096;
        if (off < h->data_end) {
            uint32_t n = h->data_end - off;
            memcpy((void *)f, p->start + off, n > 4096 ? 4096 : n);
        }
        ptab[i] = i < text_pages ? ((uint64_t)f | P_PRESENT | P_USER)
                                 : ((uint64_t)f | P_PRESENT | P_USER | P_RW | nx);
    }
    for (uint32_t i = 0; i < STACK_PAGES; i++) {
        uint32_t f = take(cur.frames, &cur.nframes);
        if (!f) { release_all(); return -1; }
        ptab[(USER_END - USER_BASE) / 4096 - STACK_PAGES + i] =
            (uint64_t)f | P_PRESENT | P_USER | P_RW | nx;
        stack_top_frame = f;
    }

    /* arguments : [argc][argv...][NULL] puis les chaines, au sommet de la pile */
    uint32_t base_virt = USER_END - 4096;
    uint32_t sp = USER_END;
    uint32_t ptrs[ARG_MAX];
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t l = (uint32_t)strlen(argv[i]) + 1;
        sp -= l;
        memcpy((void *)(stack_top_frame + (sp - base_virt)), argv[i], l);
        ptrs[i] = sp;
    }
    sp &= ~3u;
    uint32_t words[ARG_MAX + 2], nw = 0;
    words[nw++] = (uint32_t)argc;
    for (int i = 0; i < argc; i++) words[nw++] = ptrs[i];
    words[nw++] = 0;
    sp -= nw * 4;
    sp &= ~15u;
    memcpy((void *)(stack_top_frame + (sp - base_virt)), words, nw * 4);

    ((uint64_t *)pd)[0] = (uint64_t)pt | P_PRESENT | P_RW | P_USER;
    ((uint64_t *)pdpt)[0] = (uint64_t)paging_kernel_pd() | P_PRESENT;
    ((uint64_t *)pdpt)[1] = (uint64_t)pd | P_PRESENT;

    cur.pt = ptab;
    cur.pid = next_pid++;
    cur.uid = uid;
    strncpy(cur.name, prog, sizeof(cur.name) - 1);
    ctrlc = 0;
    cur.active = 1;

    __asm__ volatile ("mov %0, %%cr3" : : "r"(pdpt) : "memory");
    int status = (int)user_enter(USER_BASE + h->entry, sp);
    __asm__ volatile ("mov %0, %%cr3" : : "r"(paging_kernel_pdpt()) : "memory");
    __asm__ volatile ("sti");

    cur.active = 0;
    release_all();
    return status;
}

/* --- fautes et appels systeme --- */

void user_fault(struct regs *r) {
    uint32_t cr2 = 0;
    if (r->int_no == 14)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    vga_set_color(12, 0);
    kprintf("\nprocessus %s (pid %u) tué : %s", cur.name, cur.pid, exc_name(r->int_no));
    if (r->int_no == 14) kprintf(" à l'adresse %08x", cr2);
    kprintf(" (eip=%08x)\n", r->eip);
    vga_set_color(7, 0);
    user_abort(USER_FAULTED);
}

static void put_user_text(const uint8_t *s, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = s[i];
        /* pas de caracteres de controle : ni sequences d'echappement vers le terminal serie */
        if (c == '\n' || c == '\t' || (c >= 32 && c != 127)) kputc((char)c);
        else kputc('.');
    }
}

static int32_t sys_read_line(uint32_t buf, uint32_t len) {
    if (len < 2 || len > 1024 || !uptr_ok(buf, len, 1)) return -1;
    char *out = (char *)buf;
    uint32_t n = 0;
    for (;;) {
        if (user_kill_pending()) user_abort(USER_KILLED);
        int k = keyboard_getc();
        if (k == '\n') { kputc('\n'); break; }
        if (k == '\b') {
            while (n > 0 && ((uint8_t)out[n - 1] & 0xC0) == 0x80) n--;   /* octets de suite */
            if (n > 0) { n--; kputc('\b'); }
            continue;
        }
        if (k < 32 || k == 127 || k >= 0x110000) continue;
        char enc[4];
        uint32_t el = utf8_encode((uint32_t)k, enc);
        if (n + el >= len) continue;
        for (uint32_t i = 0; i < el; i++) {
            out[n++] = enc[i];
            kputc(enc[i]);
        }
    }
    out[n] = 0;
    return (int32_t)n;
}

void syscall_dispatch(struct regs *r) {
    uint32_t a = r->ebx, b = r->ecx, c = r->edx, d = r->esi;
    int32_t ret = -1;
    char name[FS_NAME_MAX];

    if (!cur.active) { r->eax = (uint32_t)-1; return; }

    switch (r->eax) {
    case 0:                                              /* exit(code) */
        user_abort((int)a);
    case 1:                                              /* write(fd, buf, len) */
        if ((a == 1 || a == 2) && c <= 4096 && uptr_ok(b, c, 0)) {
            put_user_text((const uint8_t *)b, c);
            ret = (int32_t)c;
        }
        break;
    case 2:                                              /* read(0, buf, len) : une ligne */
        if (a == 0) ret = sys_read_line(b, c);
        break;
    case 3: ret = (int32_t)cur.uid; break;               /* getuid */
    case 4: ret = (int32_t)cur.pid; break;               /* getpid */
    case 5: ret = (int32_t)timer_ticks(); break;         /* ticks */
    case 6: {                                            /* fread(nom, buf, len) */
        const uint8_t *data;
        uint32_t flen;
        if (copy_user_string(a, name, sizeof(name)) || !uptr_ok(b, c, 1)) break;
        int e = fs_read(name, &data, &flen, cur.uid);
        if (e) { ret = e; break; }
        if (flen > c) flen = c;
        memcpy((void *)b, data, flen);
        ret = (int32_t)flen;
        break;
    }
    case 7:                                              /* fwrite(nom, buf, len, ajout) */
        if (copy_user_string(a, name, sizeof(name)) || c > FS_FILE_MAX || !uptr_ok(b, c, 0)) break;
        ret = fs_write(name, (const void *)b, c, d != 0, cur.uid);
        if (ret == FS_OK) ret = (int32_t)c;
        break;
    case 8:                                              /* sleep(ms) */
        if (a > 10000) break;
        for (uint32_t t = 0; t < a; t += 10) {
            if (user_kill_pending()) user_abort(USER_KILLED);
            timer_sleep_ms(10);
        }
        ret = 0;
        break;
    case 9:                                              /* unlink(nom) */
        if (copy_user_string(a, name, sizeof(name))) break;
        ret = fs_remove(name, cur.uid);
        break;
    default:
        break;
    }
    r->eax = (uint32_t)ret;
}
