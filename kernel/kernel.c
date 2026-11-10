#include "lilos.h"

extern char _end[];

static void ok(const char *what) {
    vga_set_color(10, 0);
    kputs("[ ok ] ");
    vga_set_color(7, 0);
    kprintf("%s\n", what);
}

static int heap_selftest(void) {
    void *a = kmalloc(100);
    void *b = kmalloc(200);
    if (!a || !b) return 0;
    kfree(a);
    kfree(b);
    struct heap_stats s;
    heap_stats(&s);
    return s.used == 0 && s.blocks == 1;
}

void kmain(void) {
    serial_init();
    vga_init();

    vga_set_color(11, 0);
    kprintf("LilOS %s\n", LILOS_VERSION);
    vga_set_color(7, 0);
    kprintf("kernel de %u Ko chargé à 0x10000\n\n", ((uint32_t)_end - 0x10000) / 1024);

    gdt_init();
    ok("GDT");
    idt_init();
    ok("IDT et PIC (IRQ 32-47)");
    timer_init(TIMER_HZ);
    ok("timer PIT à 100 Hz");
    keyboard_init();
    ok("clavier PS/2 (AZERTY)");
    paging_init();
    ok("paging PAE (32 Mo, W^X, NX, page 0 protégée)");
    heap_init(HEAP_START, HEAP_END);
    if (!heap_selftest())
        panic("autotest du heap en échec");
    ok("heap (15 Mo)");

    if (rtl8139_init())
        ok("réseau RTL8139");
    else
        kputs("[    ] pas de carte réseau RTL8139\n");

    __asm__ volatile ("sti");
    kprintf("\nLilOS prêt. Tapez aide.\n\n");

    for (;;) {
        uint32_t uid = users_login();
        shell_run(uid);
    }
}
