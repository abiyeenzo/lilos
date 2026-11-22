#define _GNU_SOURCE
/* Fuzz hote de la pile reseau : paquets valides, mutes et aleatoires.
 * Verifie les reponses attendues, l'absence de reponse aux paquets invalides,
 * et qu'aucune lecture hors limites n'a lieu (ASan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lilos.h"

#define FAIL(...) do { printf("ECHEC: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

static uint8_t sent[16][1600];
static uint32_t sent_len[16];
static int nsent;
int nic_send(const uint8_t *f, uint32_t len) {
    if (nsent < 16) { memcpy(sent[nsent], f, len); sent_len[nsent] = len; nsent++; }
    return 0;
}
/* la pile n'utilise ni minuterie ni pilote : rien d'autre a simuler */

static const uint8_t OUR_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t PEER_MAC[6] = { 0x52, 0x55, 0x0A, 0x00, 0x02, 0x02 };
#define PEER IP4(10, 0, 2, 2)

static void w16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void w32(uint8_t *p, uint32_t v) { w16(p, v >> 16); w16(p + 2, (uint16_t)v); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint16_t csum(const uint8_t *d, uint32_t n) {
    uint32_t s = 0;
    for (; n > 1; d += 2, n -= 2) s += (uint32_t)(d[0] << 8 | d[1]);
    if (n) s += (uint32_t)d[0] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)~s;
}

/* trames de test */
static uint32_t mk_arp(uint8_t *f, int op, uint32_t spa, uint32_t tpa, const uint8_t *dst) {
    memcpy(f, dst, 6); memcpy(f + 6, PEER_MAC, 6); w16(f + 12, 0x0806);
    uint8_t *p = f + 14;
    w16(p, 1); w16(p + 2, 0x0800); p[4] = 6; p[5] = 4; w16(p + 6, (uint16_t)op);
    memcpy(p + 8, PEER_MAC, 6); w32(p + 14, spa); memset(p + 18, 0, 6); w32(p + 24, tpa);
    return 14 + 28;
}

static uint32_t mk_ip(uint8_t *f, uint32_t src, uint32_t dst, uint8_t proto, const uint8_t *pay, uint32_t plen) {
    memcpy(f, OUR_MAC, 6); memcpy(f + 6, PEER_MAC, 6); w16(f + 12, 0x0800);
    uint8_t *p = f + 14;
    p[0] = 0x45; p[1] = 0; w16(p + 2, (uint16_t)(20 + plen)); w16(p + 4, 1); w16(p + 6, 0x4000);
    p[8] = 64; p[9] = proto; w16(p + 10, 0); w32(p + 12, src); w32(p + 16, dst);
    w16(p + 10, csum(p, 20));
    memcpy(p + 20, pay, plen);
    return 14 + 20 + plen;
}

static uint32_t mk_echo(uint8_t *f, uint32_t src, uint32_t dst, uint16_t id, uint16_t seq, uint32_t plen) {
    uint8_t m[8 + 200];
    m[0] = 8; m[1] = 0; w16(m + 2, 0); w16(m + 4, id); w16(m + 6, seq);
    for (uint32_t i = 0; i < plen; i++) m[8 + i] = (uint8_t)(i * 7);
    w16(m + 2, csum(m, 8 + plen));
    return mk_ip(f, src, dst, 1, m, 8 + plen);
}

static int udp_hits;
static uint8_t udp_data[2000];
static uint32_t udp_len, udp_from;
static uint16_t udp_sport;
static void on_udp(uint32_t src, uint16_t sport, const uint8_t *d, uint32_t n) {
    udp_hits++; udp_from = src; udp_sport = sport; udp_len = n;
    memcpy(udp_data, d, n);
}

static uint32_t mk_udp(uint8_t *f, uint32_t src, uint32_t dst, uint16_t sp, uint16_t dp, const uint8_t *d, uint32_t n, int good_csum) {
    uint8_t seg[1500];
    w16(seg, sp); w16(seg + 2, dp); w16(seg + 4, (uint16_t)(8 + n)); w16(seg + 6, 0);
    memcpy(seg + 8, d, n);
    uint8_t ph[12];
    w32(ph, src); w32(ph + 4, dst); ph[8] = 0; ph[9] = 17; w16(ph + 10, (uint16_t)(8 + n));
    uint8_t buf[1600];
    memcpy(buf, ph, 12); memcpy(buf + 12, seg, 8 + n);
    uint16_t c = csum(buf, 12 + 8 + n);
    w16(seg + 6, good_csum ? (c ? c : 0xFFFF) : (uint16_t)(c ^ 0x1234));
    return mk_ip(f, src, dst, 17, seg, 8 + n);
}


/* reponse DHCP du serveur (offre ou ACK) */
static uint32_t mk_dhcp_reply(uint8_t *b, uint8_t type, uint32_t xid, uint32_t yi) {
    memset(b, 0, 240);
    b[0] = 2; b[1] = 1; b[2] = 6; w32(b + 4, xid); w32(b + 16, yi);
    memcpy(b + 28, OUR_MAC, 6); w32(b + 236, 0x63825363);
    uint8_t *o = b + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 54; *o++ = 4; w32(o, PEER); o += 4;
    *o++ = 1;  *o++ = 4; w32(o, IP4(255, 255, 255, 0)); o += 4;
    *o++ = 3;  *o++ = 4; w32(o, PEER); o += 4;
    *o++ = 6;  *o++ = 4; w32(o, IP4(10, 0, 2, 3)); o += 4;
    *o++ = 51; *o++ = 4; w32(o, 86400); o += 4;
    *o++ = 255;
    return (uint32_t)(o - b);
}

static uint32_t dhcp_xid_seen;
static int dhcp_rx_ok;
static void on_dhcp(uint32_t src, uint16_t sport, const uint8_t *d, uint32_t n) {
    uint8_t t; struct dhcp_lease l;
    (void)src; (void)sport;
    if (!dhcp_parse(d, n, 0x12345678, &t, &l)) dhcp_rx_ok++;
    dhcp_xid_seen++;
}

int main(void) {
    uint8_t f[1600];
    uint32_t n;
    net_init(OUR_MAC);
    net_ip = IP4(10, 0, 2, 15); net_mask = IP4(255, 255, 255, 0);
    net_gw = IP4(10, 0, 2, 2); net_dns = IP4(10, 0, 2, 3);
    static const uint8_t BC[6] = { 255, 255, 255, 255, 255, 255 };

    /* --- ARP --- */
    n = mk_arp(f, 1, PEER, net_ip, BC);
    nsent = 0; net_rx(f, n);
    if (nsent != 1 || rd16(sent[0] + 12) != 0x0806 || rd16(sent[0] + 20) != 2 || memcmp(sent[0], PEER_MAC, 6)) FAIL("reponse ARP");
    if (memcmp(sent[0] + 22, OUR_MAC, 6)) FAIL("MAC de la reponse ARP");
    uint8_t m[6];
    if (!net_arp_lookup(PEER, m) || memcmp(m, PEER_MAC, 6)) FAIL("adresse apprise sur requete");

    n = mk_arp(f, 1, PEER, IP4(10, 0, 2, 99), BC);          /* pour une autre machine */
    nsent = 0; net_rx(f, n);
    if (nsent) FAIL("repond a une requete ARP pour un tiers");

    uint32_t stranger = IP4(10, 0, 2, 77);
    n = mk_arp(f, 2, stranger, net_ip, (uint8_t *)OUR_MAC);   /* reponse non sollicitee (empoisonnement) */
    net_rx(f, n);
    if (net_arp_lookup(stranger, m)) FAIL("reponse ARP non sollicitee acceptee");
    net_arp_request(stranger);
    n = mk_arp(f, 2, stranger, net_ip, (uint8_t *)OUR_MAC);
    net_rx(f, n);
    if (!net_arp_lookup(stranger, m)) FAIL("reponse ARP sollicitee refusee");
    printf("arp: ok\n");

    /* --- ICMP --- */
    n = mk_echo(f, PEER, net_ip, 0x1234, 7, 32);
    nsent = 0; net_rx(f, n);
    if (nsent != 1) FAIL("pas de reponse a l'echo");
    uint8_t *ip = sent[0] + 14;
    if (ip[9] != 1 || csum(ip, 20) != 0 || ip[20] != 0 || csum(ip + 20, sent_len[0] - 34) != 0) FAIL("reponse echo invalide");
    if (rd16(ip + 24) != 0x1234 || rd16(ip + 26) != 7 || memcmp(ip + 28, f + 14 + 20 + 8, 32)) FAIL("contenu de la reponse echo");

    n = mk_echo(f, PEER, 0xFFFFFFFF, 1, 1, 8);               /* diffusion : jamais de reponse (smurf) */
    nsent = 0; net_rx(f, n);
    if (nsent) FAIL("repond a un echo en diffusion");
    n = mk_echo(f, PEER, IP4(10, 0, 2, 255), 1, 1, 8);
    net_rx(f, n);
    if (nsent) FAIL("repond a un echo sur l'adresse de diffusion du reseau");
    n = mk_echo(f, PEER, IP4(10, 0, 2, 44), 1, 1, 8);        /* pas pour nous */
    net_rx(f, n);
    if (nsent) FAIL("repond a un paquet qui ne nous est pas destine");

    n = mk_echo(f, PEER, net_ip, 1, 1, 8);
    f[14 + 20 + 3] ^= 0xFF;                                  /* somme ICMP fausse */
    net_rx(f, n);
    if (nsent) FAIL("somme ICMP fausse acceptee");
    n = mk_echo(f, PEER, net_ip, 1, 1, 8);
    f[14 + 10] ^= 0x55;                                      /* somme IP fausse */
    net_rx(f, n);
    if (nsent) FAIL("somme IP fausse acceptee");
    n = mk_echo(f, PEER, net_ip, 1, 1, 8);
    w16(f + 14 + 6, 0x2000);                                 /* MF : fragment */
    w16(f + 14 + 10, 0); w16(f + 14 + 10, csum(f + 14, 20));
    net_rx(f, n);
    if (nsent) FAIL("fragment accepte");
    n = mk_echo(f, 0xE0000001, net_ip, 1, 1, 8);             /* source multicast */
    net_rx(f, n);
    if (nsent) FAIL("source multicast acceptee");
    n = mk_echo(f, 0, net_ip, 1, 1, 8);
    net_rx(f, n);
    if (nsent) FAIL("source 0.0.0.0 acceptee");
    n = mk_echo(f, PEER, net_ip, 1, 1, 8);
    net_rx(f, n - 3);                                        /* trame tronquee */
    if (nsent) FAIL("trame tronquee acceptee");

    /* la reponse a NOTRE ping : bon id et bonne sequence seulement */
    ping_expect(PEER, 0xBEEF, 5);
    uint8_t rep[8 + 32];
    memset(rep, 0, sizeof rep); rep[0] = 0; w16(rep + 4, 0xBEEF); w16(rep + 6, 6);
    w16(rep + 2, csum(rep, sizeof rep));
    n = mk_ip(f, PEER, net_ip, 1, rep, sizeof rep); net_rx(f, n);
    if (ping_received()) FAIL("mauvaise sequence acceptee");
    w16(rep + 6, 5); w16(rep + 4, 0xBEE0); w16(rep + 2, 0); w16(rep + 2, csum(rep, sizeof rep));
    n = mk_ip(f, PEER, net_ip, 1, rep, sizeof rep); net_rx(f, n);
    if (ping_received()) FAIL("mauvais id accepte");
    w16(rep + 4, 0xBEEF); w16(rep + 2, 0); w16(rep + 2, csum(rep, sizeof rep));
    n = mk_ip(f, IP4(10, 0, 2, 9), net_ip, 1, rep, sizeof rep); net_rx(f, n);
    if (ping_received()) FAIL("mauvaise source acceptee");
    n = mk_ip(f, PEER, net_ip, 1, rep, sizeof rep); net_rx(f, n);
    if (!ping_received()) FAIL("reponse legitime refusee");
    printf("icmp: ok\n");

    /* --- UDP --- */
    udp_bind(5353, on_udp);
    uint8_t body[300];
    for (int i = 0; i < 300; i++) body[i] = (uint8_t)i;
    n = mk_udp(f, PEER, net_ip, 4000, 5353, body, 300, 1);
    udp_hits = 0; net_rx(f, n);
    if (udp_hits != 1 || udp_len != 300 || memcmp(udp_data, body, 300) || udp_from != PEER || udp_sport != 4000) FAIL("livraison UDP");
    n = mk_udp(f, PEER, net_ip, 4000, 5353, body, 300, 0);
    net_rx(f, n);
    if (udp_hits != 1) FAIL("somme UDP fausse acceptee");
    n = mk_udp(f, PEER, net_ip, 4000, 5354, body, 10, 1);
    net_rx(f, n);
    if (udp_hits != 1) FAIL("port non lie livre");
    n = mk_udp(f, PEER, 0xFFFFFFFF, 4000, 5353, body, 10, 1);
    net_rx(f, n);
    if (udp_hits != 1) FAIL("UDP en diffusion livre");
    n = mk_udp(f, PEER, net_ip, 4000, 5353, body, 10, 1);
    w16(f + 14 + 20 + 4, 9999);                              /* longueur UDP > donnees reelles */
    net_rx(f, n);
    if (udp_hits != 1) FAIL("longueur UDP incoherente acceptee");
    n = mk_udp(f, PEER, net_ip, 4000, 5353, body, 10, 1);
    w16(f + 14 + 20 + 4, 4);                                 /* longueur UDP < en-tete */
    net_rx(f, n);
    if (udp_hits != 1) FAIL("longueur UDP trop courte acceptee");
    udp_unbind(5353);
    n = mk_udp(f, PEER, net_ip, 4000, 5353, body, 10, 1);
    net_rx(f, n);
    if (udp_hits != 1) FAIL("socket delie livre");
    printf("udp: ok\n");

    /* --- DNS : construction et analyse --- */
    uint8_t q[300], resp[600];
    int ql = dns_build_query("www.example.com", 0xABCD, q, sizeof q);
    static const uint8_t want[] = { 0xAB, 0xCD, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
        3, 'w', 'w', 'w', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1 };
    if (ql != (int)sizeof want || memcmp(q, want, sizeof want)) FAIL("requete DNS");
    const char *bad[] = { "", ".", "a..b", ".a", "a.", "a b.com", "a_b.com", "a/b", "é.com" };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        if (dns_build_query(bad[i], 1, q, sizeof q) >= 0) FAIL("nom invalide accepte : '%s'", bad[i]);
    char big[300]; memset(big, 'a', 299); big[299] = 0;
    if (dns_build_query(big, 1, q, sizeof q) >= 0) FAIL("nom trop long accepte");
    memset(big, 'a', 64); big[64] = 0;
    if (dns_build_query(big, 1, q, sizeof q) >= 0) FAIL("label de 64 octets accepte");
    if (dns_build_query("a.com", 1, q, 10) >= 0) FAIL("tampon trop petit accepte");

    /* reponse valide avec compression */
    ql = dns_build_query("a.com", 0x1111, q, sizeof q);
    memcpy(resp, q, (size_t)ql);
    w16(resp + 2, 0x8180); w16(resp + 6, 2);
    int o = ql;
    resp[o++] = 0xC0; resp[o++] = 12; w16(resp + o, 5); w16(resp + o + 2, 1); w32(resp + o + 4, 60); w16(resp + o + 8, 4); o += 10;
    memcpy(resp + o, "\xC0\xA8\x00\x01", 4); o += 4;            /* un CNAME-like d'abord : type 5, ignore */
    resp[o++] = 0xC0; resp[o++] = 12; w16(resp + o, 1); w16(resp + o + 2, 1); w32(resp + o + 4, 60); w16(resp + o + 8, 4); o += 10;
    memcpy(resp + o, "\x5D\xB8\xD8\x22", 4); o += 4;
    uint32_t got = 0;
    if (!dns_parse_response(resp, (uint32_t)o, 0x1111, &got) || got != IP4(93, 184, 216, 34)) FAIL("reponse DNS valide");
    if (dns_parse_response(resp, (uint32_t)o, 0x1112, &got)) FAIL("mauvais identifiant accepte");
    for (int cut = 0; cut < o; cut++)                         /* toute troncature : jamais de lecture hors limites */
        dns_parse_response(resp, (uint32_t)cut, 0x1111, &got);
    uint8_t loop[40];                                         /* pointeur de compression qui boucle */
    memcpy(loop, resp, 12); w16(loop + 6, 1); w16(loop + 4, 1);
    loop[12] = 0xC0; loop[13] = 12; w16(loop + 14, 1); w16(loop + 16, 1);
    dns_parse_response(loop, 18, 0x1111, &got);
    printf("dns: ok\n");

    /* --- parse_ip --- */
    uint32_t v;
    if (parse_ip("10.0.2.15", &v) || v != net_ip) FAIL("parse_ip valide");
    const char *badip[] = { "", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.", ".1.2.3", "a.b.c.d", "1.2.3.4 ", "1111.2.3.4", "1..2.3" };
    for (unsigned i = 0; i < sizeof badip / sizeof *badip; i++)
        if (!parse_ip(badip[i], &v)) FAIL("adresse invalide acceptee : '%s'", badip[i]);
    printf("parse_ip: ok\n");

    /* --- DHCP : construction, analyse stricte, rejets --- */
    {
        uint8_t b[600], t; struct dhcp_lease l;
        int n = dhcp_build(b, sizeof b, DHCP_DISCOVER, 0xCAFEBABE, 0, 0);
        if (n < 244 || b[0] != 1 || rd16(b + 10) != 0x8000 || memcmp(b + 28, OUR_MAC, 6) || b[236] != 0x63 || b[240] != 53 || b[242] != DHCP_DISCOVER)
            FAIL("dhcp_build : DISCOVER invalide");
        if (dhcp_build(b, 100, DHCP_DISCOVER, 1, 0, 0) >= 0) FAIL("dhcp_build : tampon trop petit accepte");
        n = dhcp_build(b, sizeof b, DHCP_REQUEST, 7, IP4(10, 0, 2, 20), PEER);
        if (n < 0 || memmem(b + 240, (size_t)n - 240, "\x32\x04\x0a\x00\x02\x14", 6) == NULL) FAIL("dhcp_build : REQUEST sans adresse demandee");

        uint32_t rl = mk_dhcp_reply(b, DHCP_OFFER, 0x12345678, IP4(10, 0, 2, 20));
        if (dhcp_parse(b, rl, 0x12345678, &t, &l) || t != DHCP_OFFER || l.ip != IP4(10, 0, 2, 20) ||
            l.mask != IP4(255, 255, 255, 0) || l.gw != PEER || l.dns != IP4(10, 0, 2, 3) || l.server != PEER || l.lease != 86400)
            FAIL("dhcp_parse : offre valide refusee ou mal lue");
        mk_dhcp_reply(b, DHCP_ACK, 0x12345678, IP4(10, 0, 2, 20));
        if (dhcp_parse(b, rl, 0x12345678, &t, &l) || t != DHCP_ACK) FAIL("dhcp_parse : ACK");

        uint8_t m[600];
        #define REJECT(what, stmt) do { memcpy(m, b, rl); stmt; if (!dhcp_parse(m, mlen, 0x12345678, &t, &l)) FAIL("dhcp_parse accepte : %s", what); } while (0)
        uint32_t mlen = rl;
        REJECT("mauvais xid", w32(m + 4, 1));
        REJECT("op requete", m[0] = 1);
        REJECT("mauvaise MAC", m[28] ^= 1);
        REJECT("mauvais cookie", m[236] ^= 1);
        REJECT("adresse nulle", w32(m + 16, 0));
        REJECT("adresse multicast", w32(m + 16, IP4(224, 0, 0, 1)));
        REJECT("masque non contigu", w32(m + 240 + 3 + 6 + 2, IP4(255, 0, 255, 0)));
        REJECT("option 53 de longueur 2", m[241] = 2);
        REJECT("option 1 de longueur 3", m[240 + 3 + 6 + 1] = 3);
        REJECT("type DHCP inconnu", m[242] = 9);
        REJECT("option tronquee", mlen = rl - 5);
        mlen = 243;
        REJECT("trop court", (void)0);
        mlen = rl;
        memcpy(m, b, rl); m[240 + 3 + 6 + 6 + 1] = 40;      /* longueur d'option qui depasse le paquet */
        if (!dhcp_parse(m, mlen, 0x12345678, &t, &l)) FAIL("dhcp_parse accepte : longueur d'option hors paquet");
        /* routeur hors du sous-reseau : ignore mais paquet accepte */
        memcpy(m, b, rl); w32(m + 240 + 3 + 6 + 6 + 2, IP4(192, 168, 9, 9));
        if (dhcp_parse(m, mlen, 0x12345678, &t, &l) || l.gw != 0) FAIL("dhcp_parse : routeur hors sous-reseau");
        printf("dhcp: ok\n");
    }

    /* --- fuzz : trames aleatoires et mutations de trames valides --- */
    srand(99);
    uint8_t seeds[8][1600]; uint32_t sl[8];
    sl[0] = mk_arp(seeds[0], 1, PEER, net_ip, BC);
    sl[1] = mk_echo(seeds[1], PEER, net_ip, 1, 1, 64);
    sl[2] = mk_udp(seeds[2], PEER, net_ip, 53, 5353, body, 100, 1);
    sl[3] = mk_udp(seeds[3], net_dns, net_ip, 53, 5353, resp, (uint32_t)o, 1);
    sl[4] = mk_echo(seeds[4], PEER, 0xFFFFFFFF, 1, 1, 8);
    sl[5] = mk_arp(seeds[5], 2, PEER, net_ip, (uint8_t *)OUR_MAC);
    uint8_t dh[600];
    uint32_t dl = mk_dhcp_reply(dh, DHCP_OFFER, 0x12345678, IP4(10, 0, 2, 20));
    sl[6] = mk_udp(seeds[6], PEER, 0xFFFFFFFF, 67, 68, dh, dl, 1);
    sl[7] = mk_udp(seeds[7], PEER, net_ip, 67, 68, dh, dl, 1);
    udp_bind(5353, on_udp);
    udp_bind(68, on_dhcp);
    nsent = 0; dhcp_rx_ok = 0;
    net_rx(seeds[6], sl[6]);
    if (dhcp_rx_ok != 1) FAIL("offre DHCP en diffusion non remise au gestionnaire (%d)", dhcp_rx_ok);
    {   /* la diffusion vers un autre port reste ignoree */
        uint8_t o2[1600]; uint32_t l2 = mk_udp(o2, PEER, 0xFFFFFFFF, 67, 5353, dh, dl, 1);
        int before = udp_hits; net_rx(o2, l2);
        if (udp_hits != before) FAIL("diffusion UDP hors port 68 acceptee");
    }
    for (int i = 0; i < 400000; i++) {
        int s = rand() % 8;
        uint8_t g[1800];
        uint32_t len;
        if (i % 4 == 0) {                                       /* aleatoire pur */
            len = (uint32_t)(rand() % 1700);
            for (uint32_t j = 0; j < len; j++) g[j] = (uint8_t)rand();
            if (len >= 6 && rand() % 2) memcpy(g, rand() % 2 ? OUR_MAC : BC, 6);
        } else {                                                /* mutation */
            len = sl[s];
            memcpy(g, seeds[s], len);
            int muts = 1 + rand() % 4;
            for (int k = 0; k < muts; k++) g[rand() % len] = (uint8_t)rand();
            if (rand() % 5 == 0) len = (uint32_t)rand() % (len + 1);
            if (rand() % 9 == 0 && len > 34) {                  /* somme IP recalculee : atteint le code en aval */
                w16(g + 24, 0);
                uint32_t hl = (uint32_t)(g[14] & 15) * 4;
                if (hl >= 20 && hl + 14 <= len) w16(g + 24, csum(g + 14, hl));
            }
        }
        nsent = 0;
        net_rx(g, len);
        for (int k = 0; k < nsent; k++) {
            if (sent_len[k] > 1514) FAIL("trame emise trop grande (%u)", sent_len[k]);
            if (sent_len[k] >= 34 && rd16(sent[k] + 12) == 0x0800 && csum(sent[k] + 14, 20) != 0) FAIL("trame emise avec somme IP fausse");
        }
        uint32_t d;
        {
            uint8_t ty; struct dhcp_lease dl2;
            dhcp_parse(g, len, 0x12345678, &ty, &dl2);
            if (len > 42) dhcp_parse(g + 42, len - 42, 0x12345678, &ty, &dl2);
        }
        dns_parse_response(g, len, (uint16_t)rand(), &d);
        dns_parse_response(g + 42 < g + len ? g + 42 : g, len > 42 ? len - 42 : 0, 0x1111, &d);
    }
    printf("fuzz: 400000 trames, aucun crash\n");
    return 0;
}
