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
    kprintf("kernel de %u Ko charge a 0x10000\n\n", ((uint32_t)_end - 0x10000) / 1024);

    gdt_init();
    ok("GDT");
    idt_init();
    ok("IDT et PIC (IRQ 32-47)");
    timer_init(TIMER_HZ);
    ok("timer PIT a 100 Hz");
    keyboard_init();
    ok("clavier PS/2");
    paging_init();
    ok("paging (16 Mo, page 0 protegee)");
    heap_init(HEAP_START, HEAP_END);
    if (!heap_selftest())
        panic("autotest du heap en echec");
    ok("heap (15 Mo)");

    __asm__ volatile ("sti");
    kprintf("\nLilOS pret. Tapez help.\n\n");

    shell_run();
}
