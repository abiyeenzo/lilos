#include "lilos.h"

/* Pile reseau minimale : Ethernet, ARP, IPv4, ICMP (echo), UDP, DNS.
 * Tout ce qui arrive du reseau est hostile : chaque champ est verifie avant
 * usage et les paquets douteux sont ignores sans reponse. Pas de TCP, pas
 * de fragmentation IP, pas d'IPv6. */

uint8_t  net_mac[6];
uint32_t net_ip   = IP4(10, 0, 2, 15);      /* reseau utilisateur de QEMU par defaut */
uint32_t net_mask = IP4(255, 255, 255, 0);
uint32_t net_gw   = IP4(10, 0, 2, 2);
uint32_t net_dns  = IP4(10, 0, 2, 3);

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* somme de controle Internet (RFC 1071) */
static uint32_t csum_add(uint32_t sum, const uint8_t *d, uint32_t n) {
    while (n > 1) { sum += (uint32_t)(d[0] << 8 | d[1]); d += 2; n -= 2; }
    if (n) sum += (uint32_t)d[0] << 8;
    return sum;
}
static uint16_t csum_fin(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* --- ARP --- */

#define ARP_MAX 8
static struct { int used; uint32_t ip; uint8_t mac[6]; } arp_cache[ARP_MAX];
static int arp_next;
static uint32_t arp_pending;                /* seule reponse ARP acceptee */

static void arp_learn(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp_cache[i].used && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    arp_cache[arp_next].used = 1;
    arp_cache[arp_next].ip = ip;
    memcpy(arp_cache[arp_next].mac, mac, 6);
    arp_next = (arp_next + 1) % ARP_MAX;
}

int net_arp_lookup(uint32_t ip, uint8_t mac[6]) {
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp_cache[i].used && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

int net_arp_entry(int i, uint32_t *ip, uint8_t mac[6]) {
    if (i < 0 || i >= ARP_MAX || !arp_cache[i].used) return 0;
    *ip = arp_cache[i].ip;
    memcpy(mac, arp_cache[i].mac, 6);
    return 1;
}

static uint8_t txb[1600];

static int send_eth(const uint8_t *dst, uint16_t type, const uint8_t *payload, uint32_t len) {
    if (len > 1500) return -2;
    memcpy(txb, dst, 6);
    memcpy(txb + 6, net_mac, 6);
    wr16(txb + 12, type);
    memcpy(txb + 14, payload, len);
    return nic_send(txb, 14 + len);
}

static void arp_send(uint16_t op, const uint8_t *dst_mac, uint32_t tip, const uint8_t *tmac) {
    uint8_t p[28];
    wr16(p, 1); wr16(p + 2, 0x0800); p[4] = 6; p[5] = 4; wr16(p + 6, op);
    memcpy(p + 8, net_mac, 6); wr32(p + 14, net_ip);
    memcpy(p + 18, tmac, 6); wr32(p + 24, tip);
    send_eth(dst_mac, 0x0806, p, 28);
}

void net_arp_request(uint32_t ip) {
    static const uint8_t zero[6] = { 0 };
    arp_pending = ip;
    arp_send(1, BCAST, ip, zero);
}

static void arp_rx(const uint8_t *p, uint32_t n) {
    if (n < 28) return;
    if (rd16(p) != 1 || rd16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4) return;
    uint16_t op = rd16(p + 6);
    const uint8_t *sha = p + 8;
    uint32_t spa = rd32(p + 14), tpa = rd32(p + 24);
    if (spa == 0 || spa == 0xFFFFFFFF || (sha[0] & 1)) return;      /* expediteur invalide */

    if (op == 1 && tpa == net_ip) {                 /* on nous interroge : on apprend et on repond */
        arp_learn(spa, sha);
        arp_send(2, sha, spa, sha);
    } else if (op == 2 && spa == arp_pending) {     /* reponse a NOTRE requete seulement */
        arp_learn(spa, sha);
        arp_pending = 0;
    }
}

/* --- IPv4 --- */

static int resolve_mac(uint32_t dst, uint8_t mac[6]) {
    if (dst == 0xFFFFFFFF || dst == (net_ip | ~net_mask)) {
        memcpy(mac, BCAST, 6);
        return 1;
    }
    uint32_t hop = ((dst ^ net_ip) & net_mask) == 0 ? dst : net_gw;
    if (net_arp_lookup(hop, mac)) return 1;
    net_arp_request(hop);
    return 0;
}

int net_send_ip(uint32_t dst, uint8_t proto, const uint8_t *payload, uint32_t len) {
    static uint16_t ident;
    uint8_t mac[6];
    static uint8_t pkt[1500];

    if (len > 1480) return -2;
    if (!resolve_mac(dst, mac)) return -1;          /* ARP en cours : reessayer */

    pkt[0] = 0x45; pkt[1] = 0;
    wr16(pkt + 2, (uint16_t)(20 + len));
    wr16(pkt + 4, ++ident);
    wr16(pkt + 6, 0x4000);                          /* ne pas fragmenter */
    pkt[8] = 64; pkt[9] = proto;
    wr16(pkt + 10, 0);
    wr32(pkt + 12, net_ip);
    wr32(pkt + 16, dst);
    wr16(pkt + 10, csum_fin(csum_add(0, pkt, 20)));
    memcpy(pkt + 20, payload, len);
    return send_eth(mac, 0x0800, pkt, 20 + len);
}

/* --- ICMP --- */

static struct { int waiting; uint32_t dst; uint16_t id, seq; volatile int got; } ping;

void ping_expect(uint32_t dst, uint16_t id, uint16_t seq) {
    ping.dst = dst; ping.id = id; ping.seq = seq; ping.got = 0; ping.waiting = 1;
}
int ping_received(void) { return ping.got; }

int net_icmp_echo(uint32_t dst, uint16_t id, uint16_t seq) {
    uint8_t m[8 + 32];
    m[0] = 8; m[1] = 0; wr16(m + 2, 0); wr16(m + 4, id); wr16(m + 6, seq);
    for (int i = 0; i < 32; i++) m[8 + i] = (uint8_t)('a' + i % 26);
    wr16(m + 2, csum_fin(csum_add(0, m, sizeof(m))));
    return net_send_ip(dst, 1, m, sizeof(m));
}

static void icmp_rx(uint32_t src, int unicast, const uint8_t *d, uint32_t n) {
    if (n < 8 || csum_fin(csum_add(0, d, n)) != 0) return;

    if (d[0] == 8 && d[1] == 0 && unicast) {        /* echo request : on repond (jamais en diffusion) */
        static uint8_t reply[1480];
        if (n > sizeof(reply)) return;
        memcpy(reply, d, n);
        reply[0] = 0;
        wr16(reply + 2, 0);
        wr16(reply + 2, csum_fin(csum_add(0, reply, n)));
        net_send_ip(src, 1, reply, n);
    } else if (d[0] == 0 && d[1] == 0 && ping.waiting && src == ping.dst &&
               rd16(d + 4) == ping.id && rd16(d + 6) == ping.seq) {
        ping.got = 1;
    }
}

/* --- UDP --- */

#define UDP_SOCKETS 4
static struct { uint16_t port; udp_handler_t h; } sockets[UDP_SOCKETS];

int udp_bind(uint16_t port, udp_handler_t h) {
    for (int i = 0; i < UDP_SOCKETS; i++) {
        if (!sockets[i].h) { sockets[i].port = port; sockets[i].h = h; return 0; }
    }
    return -1;
}

void udp_unbind(uint16_t port) {
    for (int i = 0; i < UDP_SOCKETS; i++)
        if (sockets[i].h && sockets[i].port == port) sockets[i].h = NULL;
}

int net_send_udp(uint32_t dst, uint16_t sport, uint16_t dport, const uint8_t *data, uint32_t len) {
    static uint8_t seg[1480];
    if (len > sizeof(seg) - 8) return -2;
    wr16(seg, sport); wr16(seg + 2, dport); wr16(seg + 4, (uint16_t)(8 + len)); wr16(seg + 6, 0);
    memcpy(seg + 8, data, len);
    uint8_t ph[12];
    wr32(ph, net_ip); wr32(ph + 4, dst); ph[8] = 0; ph[9] = 17; wr16(ph + 10, (uint16_t)(8 + len));
    uint16_t c = csum_fin(csum_add(csum_add(0, ph, 12), seg, 8 + len));
    wr16(seg + 6, c ? c : 0xFFFF);
    return net_send_ip(dst, 17, seg, 8 + len);
}

static void udp_rx(uint32_t src, uint32_t dst, int unicast, const uint8_t *d, uint32_t n) {
    if (n < 8 || !unicast) return;
    uint32_t ulen = rd16(d + 4);
    if (ulen < 8 || ulen > n) return;
    if (rd16(d + 6)) {                              /* somme de controle facultative en IPv4 */
        uint8_t ph[12];
        wr32(ph, src); wr32(ph + 4, dst); ph[8] = 0; ph[9] = 17; wr16(ph + 10, (uint16_t)ulen);
        if (csum_fin(csum_add(csum_add(0, ph, 12), d, ulen)) != 0) return;
    }
    uint16_t dport = rd16(d + 2);
    for (int i = 0; i < UDP_SOCKETS; i++) {
        if (sockets[i].h && sockets[i].port == dport) {
            sockets[i].h(src, rd16(d), d + 8, ulen - 8);
            return;
        }
    }
}

static void ip_rx(const uint8_t *p, uint32_t n) {
    if (n < 20 || (p[0] >> 4) != 4) return;
    uint32_t hl = (uint32_t)(p[0] & 15) * 4;
    if (hl < 20 || hl > n) return;
    uint32_t total = rd16(p + 2);
    if (total < hl || total > n) return;
    if (csum_fin(csum_add(0, p, hl)) != 0) return;
    if (rd16(p + 6) & 0x3FFF) return;               /* fragments : non pris en charge, ignores */

    uint32_t src = rd32(p + 12), dst = rd32(p + 16);
    int unicast = dst == net_ip;
    int bcast = dst == 0xFFFFFFFF || dst == (net_ip | ~net_mask);
    if (!unicast && !bcast) return;
    if (src == 0 || src == 0xFFFFFFFF || src >= 0xE0000000u || src == net_ip) return;   /* source invalide ou usurpee */

    const uint8_t *pay = p + hl;
    uint32_t plen = total - hl;
    if (p[9] == 1) icmp_rx(src, unicast, pay, plen);
    else if (p[9] == 17) udp_rx(src, dst, unicast, pay, plen);
}

void net_init(const uint8_t mac[6]) {
    memcpy(net_mac, mac, 6);
}

void net_rx(const uint8_t *f, uint32_t len) {
    if (len < 14 || len > 1600) return;
    if (memcmp(f, net_mac, 6) && memcmp(f, BCAST, 6)) return;       /* pas pour nous */
    uint16_t type = rd16(f + 12);
    if (type == 0x0806) arp_rx(f + 14, len - 14);
    else if (type == 0x0800) ip_rx(f + 14, len - 14);
}

/* --- DNS (client) --- */

/* Construit une requete A pour name. Retourne la taille, ou -1. */
int dns_build_query(const char *name, uint16_t id, uint8_t *out, uint32_t max) {
    uint32_t n = 12, nl = (uint32_t)strlen(name);
    if (nl == 0 || nl > 253 || max < 12 + nl + 2 + 4) return -1;
    memset(out, 0, 12);
    wr16(out, id);
    wr16(out + 2, 0x0100);                          /* requete standard, recursion demandee */
    wr16(out + 4, 1);

    uint32_t label = n++, len = 0;
    for (uint32_t i = 0; i <= nl; i++) {
        char c = name[i];
        if (c == '.' || c == 0) {
            if (len == 0 || len > 63) return -1;
            out[label] = (uint8_t)len;
            label = n++;
            len = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
                return -1;
            out[n++] = (uint8_t)c;
            len++;
        }
    }
    out[label] = 0;                                 /* racine */
    wr16(out + n, 1); wr16(out + n + 2, 1);         /* type A, classe IN */
    return (int)(n + 4);
}

/* Saute un nom DNS (labels ou pointeur de compression). Retourne le nouvel offset ou -1. */
static int dns_skip_name(const uint8_t *d, uint32_t n, uint32_t off) {
    for (int guard = 0; guard < 128; guard++) {
        if (off >= n) return -1;
        uint8_t l = d[off];
        if (l == 0) return (int)(off + 1);
        if ((l & 0xC0) == 0xC0) return off + 2 <= n ? (int)(off + 2) : -1;   /* pointeur : fin du nom */
        if (l & 0xC0) return -1;
        off += 1u + l;
    }
    return -1;
}

/* Analyse une reponse : retourne 1 et remplit *ip si un enregistrement A est present. */
int dns_parse_response(const uint8_t *d, uint32_t n, uint16_t id, uint32_t *ip) {
    if (n < 12 || rd16(d) != id) return 0;
    uint16_t flags = rd16(d + 2);
    if (!(flags & 0x8000) || (flags & 0x000F)) return 0;           /* pas une reponse, ou erreur */
    uint32_t qd = rd16(d + 4), an = rd16(d + 6);
    if (qd != 1 || an == 0 || an > 64) return 0;

    int off = dns_skip_name(d, n, 12);
    if (off < 0 || (uint32_t)off + 4 > n) return 0;
    off += 4;

    for (uint32_t i = 0; i < an; i++) {
        off = dns_skip_name(d, n, (uint32_t)off);
        if (off < 0 || (uint32_t)off + 10 > n) return 0;
        uint16_t type = rd16(d + off), cls = rd16(d + off + 2), rdlen = rd16(d + off + 8);
        off += 10;
        if ((uint32_t)off + rdlen > n) return 0;
        if (type == 1 && cls == 1 && rdlen == 4) {
            *ip = rd32(d + off);
            return 1;
        }
        off += rdlen;
    }
    return 0;
}

/* a.b.c.d -> ip. Retourne 0 si valide. */
int parse_ip(const char *s, uint32_t *ip) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return -1;
        uint32_t o = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            o = o * 10 + (uint32_t)(*s++ - '0');
            if (++digits > 3 || o > 255) return -1;
        }
        v = (v << 8) | o;
        if (i < 3) { if (*s++ != '.') return -1; }
    }
    if (*s) return -1;
    *ip = v;
    return 0;
}
