#include "lilos.h"

/* Pilote Realtek RTL8139 (la carte emulee par QEMU avec -nic model=rtl8139).
 * Reception par anneau de 8 Ko, emission sur 4 tampons, interruptions. */

#define REG_IDR0   0x00
#define REG_TSD0   0x10
#define REG_TSAD0  0x20
#define REG_RBSTART 0x30
#define REG_CR     0x37
#define REG_CAPR   0x38
#define REG_IMR    0x3C
#define REG_ISR    0x3E
#define REG_RCR    0x44
#define REG_CONFIG1 0x52

#define RX_RING    8192
#define RX_BUF     (RX_RING + 16 + 1536)     /* marge : le DMA ne doit jamais sortir de ce tampon */
#define FRAME_MAX  1536
#define QUEUE      16

static uint8_t rx_buf[RX_BUF] __attribute__((aligned(16)));
static uint8_t tx_buf[4][2048] __attribute__((aligned(16)));

static struct { uint32_t len; uint8_t data[FRAME_MAX]; } queue[QUEUE];
static volatile uint32_t qhead, qtail;

static uint16_t io;
static uint32_t rx_off;
static int present, tx_cur;
static uint32_t dropped, received, resets;

int nic_present(void) { return present; }
void nic_status(uint32_t out[8]) {
    out[0] = received; out[1] = resets; out[2] = rx_off;
    out[3] = inw(io + REG_CAPR); out[4] = inw(io + 0x3A);        /* CAPR, CBR */
    out[5] = inb(io + REG_CR); out[6] = inw(io + REG_ISR); out[7] = inw(io + REG_IMR);
}
uint32_t nic_dropped(void) { return dropped; }

static void rx_enable(void) {
    outb(io + REG_CR, 0x10);                        /* reset */
    for (int i = 0; i < 100000 && (inb(io + REG_CR) & 0x10); i++)
        ;
    rx_off = 0;
    tx_cur = 0;                                     /* le materiel reprend au descripteur 0 */
    memset(rx_buf, 0, 16);
    outl(io + REG_RBSTART, (uint32_t)rx_buf);
    outw(io + REG_CAPR, 0xFFF0);                    /* pointeur de lecture au debut de l'anneau */
    outw(io + REG_IMR, 0x0005);                     /* reception + emission */
    outl(io + REG_RCR, 0x0000000E);                 /* notre MAC, multicast, diffusion ; pas de WRAP */
    outb(io + REG_CR, 0x0C);                        /* RE + TE */
}

static void rx_drain(void) {
    while (!(inb(io + REG_CR) & 1)) {               /* tampon non vide */
        uint32_t hdr = *(volatile uint32_t *)(rx_buf + rx_off);
        uint32_t status = hdr & 0xFFFF, len = hdr >> 16;

        /* longueur absurde : l'anneau est inutilisable, on redemarre le recepteur */
        if (len < 4 || len > RX_RING - 16) {
            dropped++;
            resets++;
            rx_enable();
            return;
        }

        uint32_t flen = len - 4;                    /* sans le CRC */
        if ((status & 1) && flen >= 14 && flen <= FRAME_MAX) {
            uint32_t next = (qhead + 1) % QUEUE;
            if (next != qtail) {
                /* Sans WRAP le peripherique coupe la trame a la fin de l'anneau :
                 * on la recolle, et le DMA ne depasse jamais le tampon. */
                for (uint32_t i = 0; i < flen; i++)
                    queue[qhead].data[i] = rx_buf[(rx_off + 4 + i) % RX_RING];
                queue[qhead].len = flen;
                qhead = next;
                received++;
            } else {
                dropped++;
            }
        } else {
            dropped++;                              /* erreur, trop courte ou trop longue : on saute */
        }
        rx_off = (rx_off + len + 4 + 3) & ~3u;
        if (rx_off >= RX_RING) rx_off -= RX_RING;
        outw(io + REG_CAPR, (uint16_t)(rx_off - 16));
    }
}

static void on_irq(struct regs *r) {
    (void)r;
    uint16_t isr = inw(io + REG_ISR);
    if (!isr) return;
    outw(io + REG_ISR, isr);                        /* acquittement */
    if (isr & 0x0011) rx_drain();                   /* trame recue ou depassement du tampon */
}

int nic_send(const uint8_t *frame, uint32_t len) {
    if (!present || len > 1514) return -1;
    if (len < 60) {
        memset(tx_buf[tx_cur] + len, 0, 60 - len);
    }
    memcpy(tx_buf[tx_cur], frame, len);
    if (len < 60) len = 60;
    outl(io + REG_TSAD0 + 4 * tx_cur, (uint32_t)tx_buf[tx_cur]);
    outl(io + REG_TSD0 + 4 * tx_cur, len);
    for (int i = 0; i < 200000; i++)
        if (inl(io + REG_TSD0 + 4 * tx_cur) & (1u << 13)) break;   /* DMA termine */
    tx_cur = (tx_cur + 1) & 3;
    return 0;
}

/* Remet les trames recues a la pile reseau. Appele hors interruption. */
void net_poll(void) {
    if (!present) return;
    for (;;) {
        uint32_t flags;
        __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
        if (qtail == qhead) {
            __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
            return;
        }
        static uint8_t frame[FRAME_MAX];
        uint32_t len = queue[qtail].len;
        memcpy(frame, queue[qtail].data, len);
        qtail = (qtail + 1) % QUEUE;
        __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
        net_rx(frame, len);
    }
}

int rtl8139_init(void) {
    struct pci_dev d;
    if (!pci_find(0x10EC, 0x8139, &d)) return 0;

    uint32_t bar0 = pci_read32(d.bus, d.dev, d.fn, 0x10);
    if (!(bar0 & 1)) return 0;                      /* on attend un BAR en ports d'E/S */
    io = (uint16_t)(bar0 & 0xFFFC);

    uint32_t cmd = pci_read32(d.bus, d.dev, d.fn, 0x04);
    pci_write32(d.bus, d.dev, d.fn, 0x04, cmd | 0x05);          /* E/S + maitre du bus (DMA) */

    outb(io + REG_CONFIG1, 0x00);                   /* alimentation */
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = inb(io + REG_IDR0 + i);

    rx_enable();
    present = 1;
    irq_register((int)(pci_read32(d.bus, d.dev, d.fn, 0x3C) & 0xFF) % 16, on_irq);
    net_init(mac);
    return 1;
}
