#include "lilos.h"

#define VGA_W 80
#define VGA_H 25

static volatile uint16_t *const buf = (volatile uint16_t *)0xB8000;
static int cx, cy;
static uint8_t attr = 0x07;

static uint16_t cell(char c) { return (uint16_t)(uint8_t)c | ((uint16_t)attr << 8); }

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(cy * VGA_W + cx);
    outb(0x3D4, 14);
    outb(0x3D5, pos >> 8);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

static void scroll(void) {
    for (int i = 0; i < (VGA_H - 1) * VGA_W; i++)
        buf[i] = buf[i + VGA_W];
    for (int i = (VGA_H - 1) * VGA_W; i < VGA_H * VGA_W; i++)
        buf[i] = cell(' ');
    cy = VGA_H - 1;
}

void vga_clear(void) {
    for (int i = 0; i < VGA_W * VGA_H; i++)
        buf[i] = cell(' ');
    cx = cy = 0;
    update_cursor();
}

void vga_init(void) {
    outb(0x3D4, 0x0A);          /* curseur bloc fin */
    outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
    vga_clear();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    attr = (uint8_t)((fg & 0x0F) | ((bg & 0x0F) << 4));
}

void vga_putc(char c) {
    switch (c) {
    case '\n':
        cx = 0;
        cy++;
        break;
    case '\r':
        cx = 0;
        break;
    case '\b':
        if (cx > 0) {
            cx--;
        } else if (cy > 0) {
            cy--;
            cx = VGA_W - 1;
        }
        buf[cy * VGA_W + cx] = cell(' ');
        break;
    case '\t':
        cx = (cx + 8) & ~7;
        break;
    default:
        buf[cy * VGA_W + cx] = cell(c);
        cx++;
        break;
    }
    if (cx >= VGA_W) {
        cx = 0;
        cy++;
    }
    if (cy >= VGA_H)
        scroll();
    update_cursor();
}
