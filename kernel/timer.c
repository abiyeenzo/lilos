#include "lilos.h"

static volatile uint32_t ticks;

static void on_tick(struct regs *r) {
    (void)r;
    ticks++;
}

void timer_init(uint32_t hz) {
    uint32_t div = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);
    irq_register(0, on_tick);
}

uint32_t timer_ticks(void) {
    return ticks;
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t wait = (ms * TIMER_HZ + 999) / 1000;
    uint32_t start = ticks;
    while ((uint32_t)(ticks - start) < wait)
        __asm__ volatile ("hlt");
}
