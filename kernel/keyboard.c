#include "lilos.h"

/* Scancodes set 1, disposition US */
static const char map_normal[58] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char map_shift[58] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

#define QSIZE 128

static volatile int queue[QSIZE];
static volatile uint32_t qhead, qtail;
static int shift, ctrl, caps, extended;

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

    if (release || code >= sizeof(map_normal)) return;

    char c = map_normal[code];
    if (!c) return;

    if (c >= 'a' && c <= 'z') {
        if (ctrl) c = (char)(c & 0x1F);
        else if (shift ^ caps) c = map_shift[code];
    } else if (shift) {
        c = map_shift[code];
    }
    push((unsigned char)c);
}

void keyboard_init(void) {
    while (inb(0x64) & 1) inb(0x60);   /* vide le buffer du controleur */
    irq_register(1, on_key);
}

int keyboard_getc(void) {
    for (;;) {
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
