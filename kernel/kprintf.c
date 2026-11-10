#include "lilos.h"

void kputc(char c) {
    vga_putc(c);
    serial_putc(c);
}

void kputs(const char *s) {
    while (*s) kputc(*s++);
}

static void pad(int n, char c) {
    while (n-- > 0) kputc(c);
}

static void put_str(const char *s, int width, int left) {
    int len = (int)strlen(s);
    if (!left) pad(width - len, ' ');
    kputs(s);
    if (left) pad(width - len, ' ');
}

static void put_num(uint32_t v, uint32_t base, int upper, int neg,
                    int width, int zero, int left) {
    char tmp[34];
    int n = 0;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v) {
        tmp[n++] = digs[v % base];
        v /= base;
    }
    int total = n + neg;
    if (!left && !zero) pad(width - total, ' ');
    if (neg) kputc('-');
    if (!left && zero) pad(width - total, '0');
    while (n) kputc(tmp[--n]);
    if (left) pad(width - total, ' ');
}

static void kvprintf(const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            kputc(*fmt);
            continue;
        }
        fmt++;
        int left = 0, zero = 0, width = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        switch (*fmt) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            uint32_t m = v < 0 ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
            put_num(m, 10, 0, v < 0, width, zero, left);
            break;
        }
        case 'u':
            put_num(va_arg(ap, uint32_t), 10, 0, 0, width, zero, left);
            break;
        case 'x':
            put_num(va_arg(ap, uint32_t), 16, 0, 0, width, zero, left);
            break;
        case 'X':
            put_num(va_arg(ap, uint32_t), 16, 1, 0, width, zero, left);
            break;
        case 'p':
            kputs("0x");
            put_num((uint32_t)va_arg(ap, void *), 16, 0, 0, 8, 1, 0);
            break;
        case 'c':
            kputc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            put_str(s ? s : "(null)", width, left);
            break;
        }
        case '%':
            kputc('%');
            break;
        case '\0':
            return;
        default:
            kputc('%');
            kputc(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
