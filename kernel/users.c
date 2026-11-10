#include "lilos.h"

/* Comptes utilisateurs. Les mots de passe ne sont jamais stockes : on garde
 * un sel aleatoire et SHA-256 itere. Les comptes vivent en RAM (pas de disque),
 * donc ils sont recrees au premier demarrage de chaque session. */

#define MAX_USERS   8
#define NAME_MAX_U  16
#define PW_MIN      6
#define PW_MAX      64
#define ROUNDS      2000

struct user {
    int      used;
    uint32_t uid;
    char     name[NAME_MAX_U];
    uint8_t  salt[16];
    uint8_t  hash[32];
};

static struct user users[MAX_USERS];
static uint32_t next_uid = 1000;

/* --- hachage --- */

static void derive(const uint8_t salt[16], const char *pw, uint8_t out[32]) {
    uint8_t buf[32 + 16 + PW_MAX];
    uint32_t pl = (uint32_t)strlen(pw);
    memcpy(buf, salt, 16);
    memcpy(buf + 16, pw, pl);
    sha256(buf, 16 + pl, out);
    for (int i = 1; i < ROUNDS; i++) {
        memcpy(buf, out, 32);
        memcpy(buf + 32, salt, 16);
        memcpy(buf + 48, pw, pl);
        sha256(buf, 48 + pl, out);
    }
    memset(buf, 0, sizeof(buf));
}

static int ct_equal(const uint8_t *a, const uint8_t *b, int n) {
    uint8_t d = 0;
    for (int i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

/* Source d'entropie limitee : compteur de cycles, ticks et etat precedent.
 * Suffisant pour des sels distincts, pas pour de la cryptographie forte. */
static void random_bytes(uint8_t *out, int n) {
    static uint32_t counter;
    for (int i = 0; i < n; i += 32) {
        uint32_t lo, hi;
        __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
        uint32_t seed[5] = { lo, hi, timer_ticks(), ++counter, (uint32_t)inb(0x40) };
        uint8_t h[32];
        sha256(seed, sizeof(seed), h);
        for (int j = 0; j < 32 && i + j < n; j++) out[i + j] = h[j];
    }
}

/* --- gestion --- */

static int valid_user(const char *n) {
    size_t l = strlen(n);
    if (l == 0 || l >= NAME_MAX_U) return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= '0' && *n <= '9') || *n == '_')) return 0;
    return 1;
}

static struct user *by_name(const char *name) {
    for (int i = 0; i < MAX_USERS; i++)
        if (users[i].used && !strcmp(users[i].name, name)) return &users[i];
    return NULL;
}

int users_valid_password(const char *pw) {
    size_t l = strlen(pw);
    return l >= PW_MIN && l <= PW_MAX;
}

int users_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_USERS; i++) n += users[i].used;
    return n;
}

int users_add(const char *name, const char *pw, int is_root) {
    if (!valid_user(name) || !users_valid_password(pw)) return -1;
    if (by_name(name)) return -2;
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].used) continue;
        memset(&users[i], 0, sizeof(users[i]));
        strncpy(users[i].name, name, NAME_MAX_U - 1);
        users[i].uid = is_root ? 0 : next_uid++;
        random_bytes(users[i].salt, 16);
        derive(users[i].salt, pw, users[i].hash);
        users[i].used = 1;
        return (int)users[i].uid;
    }
    return -3;
}

/* Retourne 1 si le mot de passe est bon. Fait le meme travail pour un nom
 * inconnu, pour ne pas reveler quels comptes existent par le temps de reponse. */
int users_check(const char *name, const char *pw, uint32_t *uid) {
    static const uint8_t dummy_salt[16] = { 0 };
    struct user *u = by_name(name);
    uint8_t h[32];
    derive(u ? u->salt : dummy_salt, pw, h);
    if (!u || !ct_equal(h, u->hash, 32)) return 0;
    *uid = u->uid;
    return 1;
}

int users_set_password(const char *name, const char *pw) {
    struct user *u = by_name(name);
    if (!u) return -1;
    if (!users_valid_password(pw)) return -2;
    random_bytes(u->salt, 16);
    derive(u->salt, pw, u->hash);
    return 0;
}

int users_remove(const char *name) {
    struct user *u = by_name(name);
    if (!u || u->uid == 0) return -1;
    fs_remove_owned_by(u->uid);
    memset(u, 0, sizeof(*u));
    return 0;
}

const char *users_name(uint32_t uid) {
    for (int i = 0; i < MAX_USERS; i++)
        if (users[i].used && users[i].uid == uid) return users[i].name;
    return "?";
}

int users_find_uid(const char *name, uint32_t *uid) {
    struct user *u = by_name(name);
    if (!u) return 0;
    *uid = u->uid;
    return 1;
}

int users_list(int index, const char **name, uint32_t *uid) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (!users[i].used) continue;
        if (index-- == 0) { *name = users[i].name; *uid = users[i].uid; return 1; }
    }
    return 0;
}

/* --- saisie --- */

/* Lit une ligne sans l'afficher (mots de passe). Retourne la longueur. */
int read_secret(char *out, int max) {
    int n = 0;
    for (;;) {
        int k = keyboard_getc();
        if (k == '\n') { kputc('\n'); out[n] = 0; return n; }
        if (k == '\b') {
            while (n > 0 && ((uint8_t)out[n - 1] & 0xC0) == 0x80) n--;
            if (n > 0) n--;
        } else if (k >= 32 && k != 127 && k < 0x110000) {
            char enc[4];
            uint32_t el = utf8_encode((uint32_t)k, enc);
            if (n + (int)el < max)
                for (uint32_t i = 0; i < el; i++) out[n++] = enc[i];
        }
    }
}

static int read_name(char *out, int max) {
    int n = 0;
    for (;;) {
        int k = keyboard_getc();
        if (k == '\n') { kputc('\n'); out[n] = 0; return n; }
        if (k == '\b') {
            if (n > 0) { n--; kputc('\b'); }
        } else if (k >= 32 && k < 127 && n < max - 1) {
            out[n++] = (char)k;
            kputc((char)k);
        }
    }
}

/* Demande un mot de passe deux fois. Retourne 0 si accepte. */
int ask_new_password(char *pw, int max) {
    char again[PW_MAX + 4];
    kputs("Nouveau mot de passe (6 caractères minimum) : ");
    read_secret(pw, max);
    if (!users_valid_password(pw)) {
        kputs("Mot de passe trop court ou trop long.\n");
        return -1;
    }
    kputs("Confirmez le mot de passe : ");
    read_secret(again, sizeof(again));
    if (strcmp(pw, again)) {
        kputs("Les mots de passe ne correspondent pas.\n");
        return -1;
    }
    return 0;
}

/* Premier demarrage : aucun compte, on cree root puis un compte standard. */
static void first_boot(void) {
    char pw[PW_MAX + 4], name[NAME_MAX_U + 4];

    vga_set_color(14, 0);
    kputs("Première utilisation : création des comptes.\n");
    vga_set_color(7, 0);
    kputs("Compte administrateur (root).\n");
    while (ask_new_password(pw, sizeof(pw)))
        ;
    users_add("root", pw, 1);

    kputs("\nCompte utilisateur (laissez vide pour passer).\n");
    for (;;) {
        kputs("Nom du compte : ");
        if (!read_name(name, sizeof(name))) break;
        if (!valid_user(name)) {
            kputs("Nom invalide (a-z, 0-9, _ ; 15 caractères maximum).\n");
            continue;
        }
        if (ask_new_password(pw, sizeof(pw))) continue;
        if (users_add(name, pw, 0) >= 0) break;
        kputs("Impossible de créer ce compte.\n");
    }
    memset(pw, 0, sizeof(pw));
    kputc('\n');
}

/* Boucle de connexion. Retourne l'uid une fois authentifie. */
uint32_t users_login(void) {
    char name[NAME_MAX_U + 4], pw[PW_MAX + 4];
    int fails = 0;

    if (!users_count()) first_boot();

    for (;;) {
        kputs("lilos login : ");
        read_name(name, sizeof(name));
        kputs("Mot de passe : ");
        read_secret(pw, sizeof(pw));
        uint32_t uid;
        int ok = users_check(name, pw, &uid);
        memset(pw, 0, sizeof(pw));
        if (ok) {
            kputc('\n');
            return uid;
        }
        kputs("Identifiant ou mot de passe incorrect.\n");
        fails++;
        timer_sleep_ms(1000u * (uint32_t)(fails > 5 ? 5 : fails));   /* freine le force brut */
    }
}
