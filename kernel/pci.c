#include "lilos.h"

/* Acces a la configuration PCI par les ports 0xCF8 / 0xCFC. */

static uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, cfg_addr(bus, dev, fn, off));
    return inl(0xCFC);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, cfg_addr(bus, dev, fn, off));
    outl(0xCFC, v);
}

/* Cherche un peripherique sur les bus 0 a 3. Retourne 1 si trouve. */
int pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out) {
    for (uint8_t bus = 0; bus < 4; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0) break;         /* pas de peripherique ici */
                    continue;
                }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
                    out->bus = bus; out->dev = dev; out->fn = fn;
                    return 1;
                }
                if (fn == 0 && !(pci_read32(bus, dev, 0, 0x0C) & 0x00800000))
                    break;                      /* pas multifonction */
            }
        }
    }
    return 0;
}
