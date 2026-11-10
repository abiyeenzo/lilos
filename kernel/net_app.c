#include "lilos.h"

/* Operations bloquantes au-dessus de la pile reseau : ping et resolution DNS. */

static void wait_ms(uint32_t ms) {
    uint32_t start = timer_ticks(), ticks = (ms * TIMER_HZ + 999) / 1000;
    while ((uint32_t)(timer_ticks() - start) < ticks) {
        net_poll();
        __asm__ volatile ("hlt");
    }
}

/* Envoie un echo ICMP et attend la reponse. Retourne 0, -1 (delai) ou -2 (pas de carte). */
int net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t *rtt_ms) {
    static uint16_t seq;
    if (!nic_present()) return -2;
    uint16_t id = (uint16_t)(timer_ticks() ^ 0x4C4F), s = ++seq;
    ping_expect(ip, id, s);

    uint32_t t0 = timer_ticks(), limit = (timeout_ms * TIMER_HZ + 999) / 1000;
    uint32_t last_try = 0;
    int sent = 0;
    while ((uint32_t)(timer_ticks() - t0) < limit) {
        net_poll();
        if (ping_received()) {
            *rtt_ms = (uint32_t)(timer_ticks() - t0) * (1000 / TIMER_HZ);
            return 0;
        }
        /* tant que l'adresse MAC du voisin n'est pas connue, on relance toutes les 300 ms */
        if (!sent && (last_try == 0 || (uint32_t)(timer_ticks() - last_try) >= TIMER_HZ * 3 / 10)) {
            last_try = timer_ticks() ? timer_ticks() : 1;
            if (net_icmp_echo(ip, id, s) == 0) { sent = 1; t0 = timer_ticks(); }
        }
        __asm__ volatile ("hlt");
    }
    return -1;
}

/* --- DNS --- */

static struct { volatile int done; uint32_t ip; uint16_t id, port; } dns;

static void dns_rx(uint32_t src, uint16_t sport, const uint8_t *d, uint32_t n) {
    /* seule la reponse du serveur configure, a la bonne requete, est acceptee */
    if (src != net_dns || sport != 53 || dns.done) return;
    uint32_t ip;
    if (dns_parse_response(d, n, dns.id, &ip)) {
        dns.ip = ip;
        dns.done = 1;
    }
}

static uint32_t entropy(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ (hi * 2654435761u) ^ (timer_ticks() * 40503u);
}

int net_resolve(const char *name, uint32_t *ip) {
    if (!nic_present()) return -2;
    if (!parse_ip(name, ip)) return 0;               /* deja une adresse */

    uint8_t q[300];
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t r = entropy();
        dns.id = (uint16_t)r;
        dns.port = (uint16_t)(49152 + (r >> 16) % 16000);   /* port source aleatoire */
        dns.done = 0;
        int ql = dns_build_query(name, dns.id, q, sizeof(q));
        if (ql < 0) return -3;
        if (udp_bind(dns.port, dns_rx)) return -4;

        uint32_t t0 = timer_ticks();
        int sent = 0;
        while ((uint32_t)(timer_ticks() - t0) < TIMER_HZ * 2) {
            net_poll();
            if (dns.done) { udp_unbind(dns.port); *ip = dns.ip; return 0; }
            if (!sent && net_send_udp(net_dns, dns.port, 53, q, (uint32_t)ql) == 0) sent = 1;
            else if (!sent) wait_ms(50);              /* ARP en cours */
            __asm__ volatile ("hlt");
        }
        udp_unbind(dns.port);
    }
    return -1;
}
