#include "lilos.h"

/* Systeme de fichiers en RAM : table plate de fichiers, donnees sur le heap.
 * Chaque fichier a un proprietaire et des droits (lecture 4, ecriture 2) pour
 * le proprietaire et pour les autres. root (uid 0) passe outre. */

struct file {
    char     name[FS_NAME_MAX];
    uint8_t *data;
    uint32_t size, uid;
    uint8_t  mode;                  /* (proprietaire << 3) | autres */
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

static int may(const struct file *f, uint32_t uid, int want) {
    if (uid == 0) return 1;
    int bits = f->uid == uid ? (f->mode >> 3) & 7 : f->mode & 7;
    return (bits & want) == want;
}

int fs_write(const char *name, const void *data, uint32_t len, int append, uint32_t uid) {
    if (!valid_name(name)) return FS_ENAME;
    if (len > FS_FILE_MAX) return FS_ETOOBIG;

    struct file *f = find(name);
    if (f && !may(f, uid, 2)) return FS_EPERM;
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

    if (f->used) {
        kfree(f->data);
    } else {
        f->uid = uid;
        f->mode = FS_MODE_DEFAULT;
    }
    strncpy(f->name, name, FS_NAME_MAX - 1);
    f->name[FS_NAME_MAX - 1] = 0;
    f->data = buf;
    f->size = total;
    f->used = 1;
    return FS_OK;
}

int fs_read(const char *name, const uint8_t **data, uint32_t *len, uint32_t uid) {
    struct file *f = find(name);
    if (!f) return FS_ENOENT;
    if (!may(f, uid, 4)) return FS_EPERM;
    *data = f->data;
    *len = f->size;
    return FS_OK;
}

int fs_remove(const char *name, uint32_t uid) {
    struct file *f = find(name);
    if (!f) return FS_ENOENT;
    if (uid != 0 && f->uid != uid) return FS_EPERM;
    kfree(f->data);
    memset(f, 0, sizeof(*f));
    return FS_OK;
}

int fs_copy(const char *src, const char *dst, uint32_t uid) {
    const uint8_t *data;
    uint32_t len;
    int r = fs_read(src, &data, &len, uid);
    if (r) return r;
    return fs_write(dst, data, len, 0, uid);
}

int fs_chmod(const char *name, uint8_t mode, uint32_t uid) {
    struct file *f = find(name);
    if (!f) return FS_ENOENT;
    if (uid != 0 && f->uid != uid) return FS_EPERM;
    f->mode = mode & 077;
    return FS_OK;
}

int fs_chown(const char *name, uint32_t new_uid, uint32_t uid) {
    struct file *f = find(name);
    if (!f) return FS_ENOENT;
    if (uid != 0) return FS_EPERM;
    f->uid = new_uid;
    return FS_OK;
}

int fs_list(uint32_t index, struct fs_info *out) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        if (index-- == 0) {
            out->name = files[i].name;
            out->size = files[i].size;
            out->uid = files[i].uid;
            out->mode = files[i].mode;
            return 1;
        }
    }
    return 0;
}

/* Supprime tous les fichiers d'un utilisateur (suppression de compte). */
void fs_remove_owned_by(uint32_t uid) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && files[i].uid == uid) {
            kfree(files[i].data);
            memset(&files[i], 0, sizeof(files[i]));
        }
    }
}

const char *fs_strerror(int err) {
    switch (err) {
    case FS_ENAME:   return "nom invalide (a-z A-Z 0-9 . - _, 23 caractères max)";
    case FS_ENOENT:  return "fichier introuvable";
    case FS_EFULL:   return "trop de fichiers";
    case FS_ETOOBIG: return "fichier trop gros (4096 octets max)";
    case FS_ENOMEM:  return "mémoire insuffisante";
    case FS_EPERM:   return "permission refusée";
    default:         return "erreur";
    }
}
