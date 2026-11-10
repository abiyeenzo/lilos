#ifndef LILOS_H
#define LILOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define LILOS_VERSION "0.2.0"

#define TIMER_HZ     100
#define HEAP_START   0x100000u
#define HEAP_END     0x1000000u
#define RAM_MAP_END  0x1000000u
#define STACK_TOP    0x90000u
#define STACK_GUARD  0x7F000u

/* --- ports --- */
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void io_wait(void) { outb(0x80, 0); }

/* --- string.c --- */
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strncpy(char *dst, const char *src, size_t n);
void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    parse_uint(const char *s, uint32_t *out);

/* --- serial.c --- */
void serial_init(void);
void serial_putc(char c);

/* --- vga.c --- */
void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_set_color(uint8_t fg, uint8_t bg);

/* --- kprintf.c --- */
void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);

/* --- interrupts.c --- */
struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
};
typedef void (*isr_handler_t)(struct regs *);

void gdt_init(void);
void tss_set_cr3(uint32_t cr3);
void idt_init(void);
void irq_register(int irq, isr_handler_t h);
void isr_register(int vector, isr_handler_t h);
void panic(const char *msg);

/* --- timer.c --- */
void     timer_init(uint32_t hz);
uint32_t timer_ticks(void);
void     timer_sleep_ms(uint32_t ms);

/* --- keyboard.c --- */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
void keyboard_init(void);
int  keyboard_getc(void);

static inline void cpuid_leaf(uint32_t leaf, uint32_t *r) {
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(0));
}

/* --- paging.c --- */
void paging_init(void);
int  paging_nx_enabled(void);

/* --- heap.c --- */
struct heap_stats { uint32_t used, free, blocks; };
void  heap_init(uint32_t start, uint32_t end);
void *kmalloc(size_t n);
void *kcalloc(size_t n);
void  kfree(void *p);
void  heap_stats(struct heap_stats *s);

/* --- rtc.c --- */
struct rtc_time { uint8_t sec, min, hour, day, month; uint16_t year; };
void rtc_read(struct rtc_time *t);

/* --- shell.c --- */
void shell_run(void);

#endif
