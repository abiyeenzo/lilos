#include "lilos.h"
#include "release_key.h"

/* Verifie qu'une image a ete signee par la cle de release du projet
 * (release/lilos-release.pub.pem, copiee dans release_key.h). Retourne 1 si valide. */
int release_verify(const void *img, uint32_t len, const uint8_t sig[64]) {
    return ed25519_verify(sig, img, len, release_pubkey);
}

const uint8_t *release_pubkey_bytes(void) {
    return release_pubkey;
}
