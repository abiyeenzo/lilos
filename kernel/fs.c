#include "lilos.h"

/* Systeme de fichiers en RAM : une table plate de fichiers, donnees allouees
 * sur le heap. Tailles et noms bornes, jamais de reallocation en place. */

struct file {
    char     name[FS_NAME_MAX];
    uint8_t *data;
    uint32_t size;
    int      used;
};

static struct file files[FS_MAX_FILES];

static int valid_name(const char *n) {
    size_t len = strlen(n);
    if (len == 0 || len >= FS_NAME_MAX) return 0;
    for (; *n; n++) {
        char c = *n;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

static struct file *find(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && !strcmp(files[i].name, name))
            return &files[i];
    return NULL;
}

int fs_write(const char *name, const void *data, uint32_t len, int append) {
    if (!valid_name(name)) return FS_ENAME;
    if (len > FS_FILE_MAX) return FS_ETOOBIG;

    struct file *f = find(name);
    uint32_t old = f ? f->size : 0;
    uint32_t total = append ? old + len : len;
    if (total > FS_FILE_MAX) return FS_ETOOBIG;

    if (!f) {
        for (int i = 0; i < FS_MAX_FILES && !f; i++)
            if (!files[i].used) f = &files[i];
        if (!f) return FS_EFULL;
    }

    /* nouveau tampon d'abord : en cas d'echec l'ancien contenu reste intact */
    uint8_t *buf = kmalloc(total ? total : 1);
    if (!buf) return FS_ENOMEM;

    uint32_t keep = append ? old : 0;
    if (keep) memcpy(buf, f->data, keep);
    if (len) memcpy(buf + keep, data, len);

    if (f->used) kfree(f->data);
    strncpy(f->name, name, FS_NAME_MAX - 1);
    f->name[FS_NAME_MAX - 1] = 0;
    f->data = buf;
    f->size = total;
    f->used = 1;
    return FS_OK;
}

int fs_read(const char *name, const uint8_t **data, uint32_t *len) {
    struct file *f = find(name);
    if (!f) return FS_ENOENT;
    *data = f->data;
    *len = f->size;
    return FS_OK;
}

int fs_remove(const char *name) {
    struct file *f = find(name);
    if (!f) return FS_ENOENT;
    kfree(f->data);
    memset(f, 0, sizeof(*f));
    return FS_OK;
}

int fs_copy(const char *src, const char *dst) {
    const uint8_t *data;
    uint32_t len;
    int r = fs_read(src, &data, &len);
    if (r) return r;
    return fs_write(dst, data, len, 0);
}

int fs_list(uint32_t index, const char **name, uint32_t *size) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        if (index-- == 0) {
            *name = files[i].name;
            *size = files[i].size;
            return 1;
        }
    }
    return 0;
}

const char *fs_strerror(int err) {
    switch (err) {
    case FS_ENAME:   return "nom invalide (a-z A-Z 0-9 . - _, 23 caractères max)";
    case FS_ENOENT:  return "fichier introuvable";
    case FS_EFULL:   return "trop de fichiers";
    case FS_ETOOBIG: return "fichier trop gros (4096 octets max)";
    case FS_ENOMEM:  return "mémoire insuffisante";
    default:         return "erreur";
    }
}
