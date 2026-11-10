#include "lilos.h"

/* Scancodes set 1, disposition AZERTY francaise. Retourne un point de code
 * Unicode (les lettres accentuees sont dans Latin-1), ou 0. */
static uint32_t translate(uint8_t code, int shift, int altgr, int caps) {
    static const char top[] = "azertyuiop", mid[] = "qsdfghjklm", bot[] = "wxcvbn";
    static const uint16_t row_n[12] = { '&', 0xE9, '"', '\'', '(', '-', 0xE8, '_', 0xE7, 0xE0, ')', '=' };
    static const uint16_t row_s[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xB0, '+' };
    static const uint16_t row_a[12] = { 0, '~', '#', '{', '[', '|', '`', '\\', '^', '@', ']', '}' };
    char l = 0;

    if (code >= 0x10 && code <= 0x19) l = top[code - 0x10];
    else if (code >= 0x1E && code <= 0x27) l = mid[code - 0x1E];
    else if (code >= 0x2C && code <= 0x31) l = bot[code - 0x2C];
    if (l) return (shift ^ caps) ? (uint32_t)(l - 32) : (uint32_t)l;

    if (code >= 0x02 && code <= 0x0D) {
        if (altgr) return row_a[code - 2];
        return shift ? row_s[code - 2] : row_n[code - 2];
    }
    switch (code) {
    case 0x01: return 27;
    case 0x0E: return '\b';
    case 0x0F: return '\t';
    case 0x1C: return '\n';
    case 0x1A: return shift ? 0xA8 : '^';
    case 0x1B: return shift ? 0xA3 : '$';
    case 0x28: return shift ? '%' : 0xF9;
    case 0x29: return 0xB2;
    case 0x2B: return shift ? 0xB5 : '*';
    case 0x32: return shift ? '?' : ',';
    case 0x33: return shift ? '.' : ';';
    case 0x34: return shift ? '/' : ':';
    case 0x35: return shift ? 0xA7 : '!';
    case 0x37: return '*';
    case 0x39: return ' ';
    case 0x56: return shift ? '>' : '<';
    default:   return 0;
    }
}

#define QSIZE 128

static volatile int queue[QSIZE];
static volatile uint32_t qhead, qtail;
static int shift, ctrl, caps, altgr, extended;

static void push(int k) {
    uint32_t next = (qhead + 1) % QSIZE;
    if (next == qtail) return;
    queue[qhead] = k;
    qhead = next;
}

static void on_key(struct regs *r) {
    (void)r;
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    int release = sc & 0x80;
    uint8_t code = sc & 0x7F;

    if (extended) {
        extended = 0;
        if (code == 0x38) {             /* AltGr */
            altgr = !release;
            return;
        }
        if (release) return;
        switch (code) {
        case 0x48: push(KEY_UP); break;
        case 0x50: push(KEY_DOWN); break;
        case 0x4B: push(KEY_LEFT); break;
        case 0x4D: push(KEY_RIGHT); break;
        }
        return;
    }

    switch (code) {
    case 0x2A:
    case 0x36:
        shift = !release;
        return;
    case 0x1D:
        ctrl = !release;
        return;
    case 0x3A:
        if (!release) caps = !caps;
        return;
    }

    if (release) return;

    uint32_t c = translate(code, shift, altgr, caps);
    if (!c) return;
    if (ctrl && c == 'c') {             /* Ctrl+C : interrompt le processus utilisateur */
        user_ctrlc();
        return;
    }
    if (ctrl && c >= 'a' && c <= 'z') c &= 0x1F;
    push((int)c);
}

void keyboard_init(void) {
    while (inb(0x64) & 1) inb(0x60);   /* vide le buffer du controleur */
    irq_register(1, on_key);
}

int keyboard_getc(void) {
    for (;;) {
        net_poll();                     /* repond aux paquets (ARP, ping) pendant l'attente */
        __asm__ volatile ("cli");
        if (qtail != qhead) {
            int k = queue[qtail];
            qtail = (qtail + 1) % QSIZE;
            __asm__ volatile ("sti");
            return k;
        }
        __asm__ volatile ("sti; hlt");
    }
}
