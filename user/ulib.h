#ifndef ULIB_H
#define ULIB_H

/* Mini bibliotheque pour les programmes utilisateur : appels systeme par int 0x80. */

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETUID 3
#define SYS_GETPID 4
#define SYS_TICKS  5
#define SYS_FREAD  6
#define SYS_FWRITE 7
#define SYS_SLEEP  8
#define SYS_UNLINK 9

static inline int sys(int n, int a, int b, int c, int d) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}

static inline int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void puts_(const char *s) { sys(SYS_WRITE, 1, (int)s, slen(s), 0); }
static inline void exit_(int code) { sys(SYS_EXIT, code, 0, 0, 0); for (;;) ; }
static inline int getuid_(void) { return sys(SYS_GETUID, 0, 0, 0, 0); }
static inline int getpid_(void) { return sys(SYS_GETPID, 0, 0, 0, 0); }

static inline void putnum(int v) {
    char buf[12];
    int n = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    if (u == 0) buf[n++] = '0';
    while (u) { buf[n++] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) buf[n++] = '-';
    char out[12];
    for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
    sys(SYS_WRITE, 1, (int)out, n, 0);
}

#endif
