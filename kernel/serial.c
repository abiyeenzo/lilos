#include "lilos.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* pas d'interruptions */
    outb(COM1 + 3, 0x80);   /* DLAB */
    outb(COM1 + 0, 0x03);   /* 38400 bauds */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO */
    outb(COM1 + 4, 0x03);
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++)
        ;
    outb(COM1, (uint8_t)c);
}
