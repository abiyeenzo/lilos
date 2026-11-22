#!/bin/sh
# Ecrit kernel/release_key.h (cle publique Ed25519 brute) a partir du PEM de la release.
# Usage : sh tests/gen_release_key.sh [pem] > kernel/release_key.h
set -eu
pem="${1:-release/lilos-release.pub.pem}"
der="$(openssl pkey -pubin -in "$pem" -outform DER | od -An -v -tx1 | tr -d ' \n')"
# SubjectPublicKeyInfo Ed25519 : 12 octets d'en-tete fixe puis la cle de 32 octets
case "$der" in
  302a300506032b6570032100????????????????????????????????????????????????????????????????) ;;
  *) echo "gen_release_key : $pem n'est pas une cle publique Ed25519" >&2; exit 1 ;;
esac
key="${der#302a300506032b6570032100}"
echo "/* Genere par tests/gen_release_key.sh depuis release/lilos-release.pub.pem : ne pas editer. */"
echo "static const uint8_t release_pubkey[32] = {"
echo "$key" | sed 's/../0x&, /g; s/\(\(0x.., \)\{8\}\)/    \1\n/g' | sed 's/ *$//' | grep -v '^$'
echo "};"
