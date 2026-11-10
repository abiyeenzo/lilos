#!/bin/sh
# Assemble le site statique dans un dossier de sortie, avec la démo navigateur.
#
#   sh site/build.sh <dossier-de-sortie> --image build/lilos.img
#   sh site/build.sh <dossier-de-sortie> --release          # dernière release publiée, signature vérifiée
#
# L'émulateur (v86) et ses BIOS sont téléchargés puis vérifiés par empreinte :
# une dépendance modifiée fait échouer la construction.
set -eu

V86_VERSION="0.5.473"
V86_INTEGRITY="sha512-mNHB0DXDmfxn0CrlqCmlgMR1W7aKuD+sHmSctLcTROOwpOvbE4qsAhmaE6Vy+riQfcE4PfA2xvW59/ZXsYnHNw=="
V86_COMMIT="315b2b0b758703ef0e6cc7b4088d6068f83f3c20"
SEABIOS_SHA256="73e3f359102e3a9982c35fce98eb7cd08f18303ac7f1ba6ebfbe6cdc1c244d98"
VGABIOS_SHA256="a4bc0d80cc3ca028c73dafa8fee396b8d054ce87ebd8abfbd31b06b437607880"
REPO="abiyeenzo/lilos"

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?usage: build.sh <dossier-de-sortie> (--image <img> | --release)}"
mode="${2:?precisez --image <fichier> ou --release}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail() { echo "erreur : $*" >&2; exit 1; }

sha256_of() { sha256sum "$1" | cut -d' ' -f1; }

# --- image du système -------------------------------------------------------
case "$mode" in
  --image)
    img="${3:?chemin de l'image}"
    [ -f "$img" ] || fail "image introuvable : $img"
    ;;
  --release)
    base="https://github.com/$REPO/releases/latest/download"
    curl -fsSL --retry 3 -o "$work/lilos.img" "$base/lilos.img" || fail "pas de release publiée"
    curl -fsSL --retry 3 -o "$work/lilos.img.sig" "$base/lilos.img.sig" || fail "signature absente"
    openssl pkeyutl -verify -pubin -inkey "$root/release/lilos-release.pub.pem" \
      -rawin -in "$work/lilos.img" -sigfile "$work/lilos.img.sig" >/dev/null \
      || fail "signature de la release invalide : image refusée"
    img="$work/lilos.img"
    ;;
  *) fail "option inconnue : $mode" ;;
esac

# --- émulateur v86 ----------------------------------------------------------
curl -fsSL --retry 3 -o "$work/v86.tgz" "https://registry.npmjs.org/v86/-/v86-$V86_VERSION.tgz"
got="sha512-$(openssl dgst -sha512 -binary "$work/v86.tgz" | openssl base64 -A)"
[ "$got" = "$V86_INTEGRITY" ] || fail "empreinte de v86 inattendue ($got)"
mkdir "$work/v86"
tar xzf "$work/v86.tgz" -C "$work/v86" package/build/libv86.js package/build/v86.wasm package/LICENSE

for f in seabios vgabios; do
  curl -fsSL --retry 3 -o "$work/$f.bin" "https://raw.githubusercontent.com/copy/v86/$V86_COMMIT/bios/$f.bin"
done
[ "$(sha256_of "$work/seabios.bin")" = "$SEABIOS_SHA256" ] || fail "empreinte de seabios.bin inattendue"
[ "$(sha256_of "$work/vgabios.bin")" = "$VGABIOS_SHA256" ] || fail "empreinte de vgabios.bin inattendue"

# --- assemblage ---------------------------------------------------------------
rm -rf "$out"
mkdir -p "$out/demo"
for f in index.html docs.html 404.html style.css app.js favicon.svg; do
  cp "$root/site/$f" "$out/$f"
done
cp "$img" "$out/demo/lilos.img"
cp "$work/v86/package/build/libv86.js" "$work/v86/package/build/v86.wasm" "$out/demo/"
cp "$work/seabios.bin" "$work/vgabios.bin" "$out/demo/"
cp "$work/v86/package/LICENSE" "$out/demo/LICENSE-v86.txt"
cp "$root/release/lilos-release.pub.pem" "$out/lilos-release.pub.pem"
touch "$out/.nojekyll"
echo "site assemblé dans $out ($(du -sh "$out" | cut -f1))"
