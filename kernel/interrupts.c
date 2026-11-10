#include "lilos.h"

/* --- GDT --- */

struct gdt_entry {
    uint16_t limit_low, base_low;
    uint8_t  base_mid, access, gran, base_high;
} __attribute__((packed));

struct dt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[5];
static struct dt_ptr gdtp;

struct tss {
    uint32_t link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} __attribute__((packed));

#define SEL_TSS_MAIN 0x18
#define SEL_TSS_DF   0x20

static struct tss tss_main, tss_df;
static uint8_t df_stack[4096] __attribute__((aligned(16)));

static void double_fault_task(void);

extern void gdt_flush(uint32_t);
extern void idt_flush(uint32_t);

static void gdt_set(int i, uint8_t access) {
    gdt[i].limit_low = 0xFFFF;
    gdt[i].base_low = 0;
    gdt[i].base_mid = 0;
    gdt[i].access = access;
    gdt[i].gran = 0xCF;
    gdt[i].base_high = 0;
}

static void gdt_set_tss(int i, struct tss *t) {
    uint32_t base = (uint32_t)t;
    gdt[i].limit_low = sizeof(*t) - 1;
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].access = 0x89;
    gdt[i].gran = 0x00;
    gdt[i].base_high = base >> 24;
}

/* Le double fault s'execute dans sa propre tache avec sa propre pile :
 * un debordement de pile affiche un diagnostic au lieu d'un triple fault. */
void gdt_init(void) {
    memset(&gdt[0], 0, sizeof(gdt[0]));
    gdt_set(1, 0x9A);
    gdt_set(2, 0x92);

    memset(&tss_main, 0, sizeof(tss_main));
    memset(&tss_df, 0, sizeof(tss_df));
    tss_df.eip = (uint32_t)double_fault_task;
    tss_df.esp = (uint32_t)df_stack + sizeof(df_stack);
    tss_df.eflags = 2;
    tss_df.cs = 0x08;
    tss_df.ss = tss_df.ds = tss_df.es = tss_df.fs = tss_df.gs = 0x10;
    gdt_set_tss(3, &tss_main);
    gdt_set_tss(4, &tss_df);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint32_t)gdt;
    gdt_flush((uint32_t)&gdtp);
    __asm__ volatile ("ltr %%ax" : : "a"(SEL_TSS_MAIN));
}

void tss_set_cr3(uint32_t cr3) {
    tss_df.cr3 = cr3;
}

/* --- IDT --- */

struct idt_entry {
    uint16_t base_low, sel;
    uint8_t  zero, flags;
    uint16_t base_high;
} __attribute__((packed));

#define VECTORS 48

extern uint32_t isr_stub_table[VECTORS];

static struct idt_entry idt[256];
static struct dt_ptr idtp;
static isr_handler_t handlers[VECTORS];

/* --- PIC 8259 --- */

#define PIC1_CMD 0x20
#define PIC1_DAT 0x21
#define PIC2_CMD 0xA0
#define PIC2_DAT 0xA1

static void pic_remap(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DAT, 32);   io_wait();
    outb(PIC2_DAT, 40);   io_wait();
    outb(PIC1_DAT, 0x04); io_wait();
    outb(PIC2_DAT, 0x02); io_wait();
    outb(PIC1_DAT, 0x01); io_wait();
    outb(PIC2_DAT, 0x01); io_wait();
    outb(PIC1_DAT, 0xFB);           /* tout masque sauf la cascade (IRQ2) */
    outb(PIC2_DAT, 0xFF);
}

static void irq_unmask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DAT : PIC2_DAT;
    uint8_t bit = (uint8_t)(1 << (irq & 7));
    outb(port, inb(port) & ~bit);
}

void idt_init(void) {
    for (int i = 0; i < VECTORS; i++) {
        uint32_t base = isr_stub_table[i];
        idt[i].base_low = base & 0xFFFF;
        idt[i].sel = 0x08;
        idt[i].zero = 0;
        idt[i].flags = 0x8E;
        idt[i].base_high = base >> 16;
    }
    /* vecteur 8 : task gate vers le TSS du double fault */
    idt[8].base_low = 0;
    idt[8].sel = SEL_TSS_DF;
    idt[8].zero = 0;
    idt[8].flags = 0x85;
    idt[8].base_high = 0;
    pic_remap();
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)idt;
    idt_flush((uint32_t)&idtp);
}

void isr_register(int vector, isr_handler_t h) {
    if (vector >= 0 && vector < VECTORS)
        handlers[vector] = h;
}

void irq_register(int irq, isr_handler_t h) {
    handlers[32 + irq] = h;
    irq_unmask(irq);
    if (irq >= 8) irq_unmask(2);
}

/* --- exceptions --- */

static const char *const exc_names[32] = {
    "Division par zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound range exceeded", "Instruction invalide", "Coprocesseur absent",
    "Double fault", "Coprocessor segment overrun", "TSS invalide",
    "Segment absent", "Stack fault", "General protection fault",
    "Page fault", "Reserve", "Erreur x87", "Alignment check",
    "Machine check", "Erreur SIMD", "Virtualisation", "Control protection",
    "Reserve", "Reserve", "Reserve", "Reserve", "Reserve", "Reserve",
    "Hypervisor injection", "VMM communication", "Security exception",
    "Reserve"
};

static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

void panic(const char *msg) {
    __asm__ volatile ("cli");
    vga_set_color(15, 4);
    kprintf("\n*** KERNEL PANIC: %s ***\n", msg);
    halt_forever();
}

static void double_fault_task(void) {
    uint32_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    vga_set_color(15, 4);
    kprintf("\n*** DOUBLE FAULT ***\n");
    kprintf("pile corrompue ou debordee ? dernier cr2=%08x\n", cr2);
    halt_forever();
}

static void exception(struct regs *r) {
    uint32_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("cli");
    vga_set_color(15, 4);
    kprintf("\n*** EXCEPTION %u: %s ***\n", r->int_no, exc_names[r->int_no]);
    kprintf("eip=%08x cs=%04x eflags=%08x err=%08x\n",
            r->eip, r->cs, r->eflags, r->err_code);
    kprintf("eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("esi=%08x edi=%08x ebp=%08x esp=%08x\n",
            r->esi, r->edi, r->ebp, r->esp);
    if (r->int_no == 14)
        kprintf("adresse fautive (cr2)=%08x\n", cr2);
    halt_forever();
}

void isr_handler(struct regs *r) {
    uint32_t n = r->int_no;

    if (n < 32) {
        if (handlers[n]) handlers[n](r);
        else exception(r);
        return;
    }

    /* IRQ parasites (7 et 15) : pas d'EOI si le bit ISR n'est pas positionne */
    if (n == 39) {
        outb(PIC1_CMD, 0x0B);
        if (!(inb(PIC1_CMD) & 0x80)) return;
    } else if (n == 47) {
        outb(PIC2_CMD, 0x0B);
        if (!(inb(PIC2_CMD) & 0x80)) {
            outb(PIC1_CMD, 0x20);
            return;
        }
    }

    if (handlers[n]) handlers[n](r);
    if (n >= 40) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
