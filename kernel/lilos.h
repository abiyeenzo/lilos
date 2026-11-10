#ifndef LILOS_H
#define LILOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define LILOS_VERSION "0.3.0"

#define TIMER_HZ     100
#define HEAP_START   0x100000u
#define HEAP_END     0x1000000u
#define RAM_MAP_END  0x2000000u      /* 32 Mo : 0-16 Mo noyau et heap, 16-32 Mo pages des processus */
#define FRAME_POOL_START 0x1000000u
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
static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
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
int    memcmp(const void *a, const void *b, size_t n);
int    parse_uint(const char *s, uint32_t *out);
int    parse_int(const char *s, int32_t *out);

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
    uint32_t useresp, ss;               /* valides seulement si cs & 3 */
};
typedef void (*isr_handler_t)(struct regs *);

void gdt_init(void);
void tss_set_cr3(uint32_t cr3);
void idt_init(void);
void irq_register(int irq, isr_handler_t h);
void isr_register(int vector, isr_handler_t h);
void panic(const char *msg);
const char *exc_name(uint32_t n);

/* --- timer.c --- */
void     timer_init(uint32_t hz);
uint32_t timer_ticks(void);
void     timer_sleep_ms(uint32_t ms);

/* --- keyboard.c --- */
/* keyboard_getc retourne un point de code Unicode (disposition AZERTY)
 * ou une des touches speciales ci-dessous. */
#define KEY_UP    0x200000
#define KEY_DOWN  0x200001
#define KEY_LEFT  0x200002
#define KEY_RIGHT 0x200003
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
int  paging_range_mapped(uint32_t addr, uint32_t len);

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

/* --- fs.c (systeme de fichiers en RAM) --- */
#define FS_MAX_FILES 32
#define FS_NAME_MAX  24
#define FS_FILE_MAX  4096
#define FS_OK      0
#define FS_ENAME  (-1)
#define FS_ENOENT (-2)
#define FS_EFULL  (-3)
#define FS_ETOOBIG (-4)
#define FS_ENOMEM (-5)
struct fs_info { const char *name; uint32_t size, uid; uint8_t mode; };
#define FS_EPERM (-6)
#define FS_MODE_DEFAULT 064         /* proprietaire lecture+ecriture, autres lecture */
int  fs_write(const char *name, const void *data, uint32_t len, int append, uint32_t uid);
int  fs_read(const char *name, const uint8_t **data, uint32_t *len, uint32_t uid);
int  fs_remove(const char *name, uint32_t uid);
int  fs_copy(const char *src, const char *dst, uint32_t uid);
int  fs_chmod(const char *name, uint8_t mode, uint32_t uid);
int  fs_chown(const char *name, uint32_t new_uid, uint32_t uid);
int  fs_list(uint32_t index, struct fs_info *out);
void fs_remove_owned_by(uint32_t uid);
const char *fs_strerror(int err);

/* --- user.c : processus en mode utilisateur --- */
#define USER_BASE   0x40000000u
#define USER_END    0x40100000u
#define USER_KILLED  (-2)
#define USER_FAULTED (-3)
int      user_run(const char *prog, int argc, char **argv, uint32_t uid);
int      user_program_count(void);
const char *user_program_name(int i);
void     syscall_dispatch(struct regs *r);
void     user_fault(struct regs *r);
void     user_abort(int status) __attribute__((noreturn));
int      user_kill_pending(void);
void     user_ctrlc(void);
int      user_running(void);
uint32_t paging_kernel_pdpt(void);
uint32_t paging_kernel_pd(void);
int      paging_nx_on(void);
uint32_t utf8_encode(uint32_t cp, char *out);

/* --- pci.c, rtl8139.c, net.c, net_app.c : reseau --- */
struct pci_dev { uint8_t bus, dev, fn; };
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);
int      pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out);

int      rtl8139_init(void);
int      nic_present(void);
uint32_t nic_dropped(void);
void     nic_status(uint32_t out[8]);
int      nic_send(const uint8_t *frame, uint32_t len);
void     net_poll(void);

#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
extern uint8_t  net_mac[6];
extern uint32_t net_ip, net_mask, net_gw, net_dns;

typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port, const uint8_t *data, uint32_t len);
void     net_init(const uint8_t mac[6]);
void     net_rx(const uint8_t *frame, uint32_t len);
int      net_send_ip(uint32_t dst, uint8_t proto, const uint8_t *payload, uint32_t len);
int      net_send_udp(uint32_t dst, uint16_t sport, uint16_t dport, const uint8_t *data, uint32_t len);
void     net_arp_request(uint32_t ip);
int      net_arp_lookup(uint32_t ip, uint8_t mac[6]);
int      net_arp_entry(int i, uint32_t *ip, uint8_t mac[6]);
int      udp_bind(uint16_t port, udp_handler_t h);
void     udp_unbind(uint16_t port);
int      net_icmp_echo(uint32_t dst, uint16_t id, uint16_t seq);
void     ping_expect(uint32_t dst, uint16_t id, uint16_t seq);
int      ping_received(void);
int      dns_build_query(const char *name, uint16_t id, uint8_t *out, uint32_t max);
int      dns_parse_response(const uint8_t *d, uint32_t n, uint16_t id, uint32_t *ip);
int      net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t *rtt_ms);
int      net_resolve(const char *name, uint32_t *ip);
int      parse_ip(const char *s, uint32_t *ip);

/* --- sha256.c, users.c --- */
void     sha256(const void *data, uint32_t len, uint8_t out[32]);
int      users_valid_password(const char *pw);
int      users_count(void);
int      users_add(const char *name, const char *pw, int is_root);
int      users_check(const char *name, const char *pw, uint32_t *uid);
int      users_set_password(const char *name, const char *pw);
int      users_remove(const char *name);
const char *users_name(uint32_t uid);
int      users_find_uid(const char *name, uint32_t *uid);
int      users_list(int index, const char **name, uint32_t *uid);
int      read_secret(char *out, int max);
int      ask_new_password(char *pw, int max);
uint32_t users_login(void);

/* --- shell.c --- */
void shell_run(uint32_t uid);

#endif
