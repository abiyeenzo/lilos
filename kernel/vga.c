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

/* Point de code Unicode vers la page de code 437 du VGA. */
static uint8_t cp437(uint32_t cp) {
    switch (cp) {
    case 0xC7: return 0x80;  case 0xFC: return 0x81;  case 0xE9: return 0x82;
    case 0xE2: return 0x83;  case 0xE4: return 0x84;  case 0xE0: return 0x85;
    case 0xE7: return 0x87;  case 0xEA: return 0x88;  case 0xEB: return 0x89;
    case 0xE8: return 0x8A;  case 0xEF: return 0x8B;  case 0xEE: return 0x8C;
    case 0xC9: return 0x90;  case 0xF4: return 0x93;  case 0xF6: return 0x94;
    case 0xFB: return 0x96;  case 0xF9: return 0x97;  case 0xA3: return 0x9C;
    case 0xB0: return 0xF8;  case 0xB2: return 0xFD;  case 0xB5: return 0xE6;
    case 0xA7: return 0x15;  case 0xAB: return 0xAE;  case 0xBB: return 0xAF;
    case 0xB7: return 0xFA;  case 0xA8: return 0xF9;  case 0xA4: return 0x0F;
    case 0xC0: case 0xC2: return 'A';
    case 0xC8: case 0xCA: case 0xCB: case 0x20AC: return 'E';
    case 0xCE: case 0xCF: return 'I';
    case 0xD4: return 'O';
    case 0xD9: case 0xDB: return 'U';
    default: return '?';
    }
}

/* Decodeur UTF-8 : retourne l'octet VGA a afficher, ou -1 si la sequence
 * n'est pas terminee. Les octets invalides s'affichent comme '?'. */
static int utf8_feed(uint8_t b) {
    static uint32_t cp;
    static int need;

    if (need) {
        if ((b & 0xC0) == 0x80) {
            cp = (cp << 6) | (b & 0x3F);
            if (--need) return -1;
            return cp437(cp);
        }
        need = 0;                       /* sequence interrompue */
    }
    if (b < 0x80) return b;
    if (b >= 0xC0 && b < 0xE0) { cp = b & 0x1F; need = 1; return -1; }
    if (b >= 0xE0 && b < 0xF0) { cp = b & 0x0F; need = 2; return -1; }
    if (b >= 0xF0 && b < 0xF8) { cp = b & 0x07; need = 3; return -1; }
    return '?';
}

void vga_putc(char ch) {
    int g = utf8_feed((uint8_t)ch);
    if (g < 0) return;
    char c = (char)g;
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
