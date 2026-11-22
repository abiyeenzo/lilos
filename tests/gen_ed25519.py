#!/usr/bin/env python3
"""Genere les vecteurs de test d'Ed25519 et de SHA-512, avec OpenSSL et hashlib comme references.

Chaque ligne :  ed <pk> <sig> <msg> <attendu 0|1>
                sha <msg> <digest>
(tout en hexadecimal)

Usage : python3 tests/gen_ed25519.py > build/ed25519.vec
"""
import hashlib, os, random, subprocess, sys, tempfile

P = 2**255 - 19
Lq = 2**252 + 27742317777372353535851937790883648493
rnd = random.Random(2026)
tmp = tempfile.mkdtemp()
out = []

def run(*a, **k): return subprocess.run(a, capture_output=True, **k)

def keypair():
    pem = os.path.join(tmp, "k.pem")
    run("openssl", "genpkey", "-algorithm", "ED25519", "-out", pem)
    pub = run("openssl", "pkey", "-in", pem, "-pubout", "-outform", "DER").stdout[-32:]
    return pem, pub

def sign(pem, msg):
    m = os.path.join(tmp, "m.bin"); s = os.path.join(tmp, "s.bin")
    open(m, "wb").write(msg)
    r = run("openssl", "pkeyutl", "-sign", "-rawin", "-inkey", pem, "-in", m, "-out", s)
    assert r.returncode == 0, r.stderr
    return open(s, "rb").read()

def openssl_accepts(pub, msg, sig):
    # reference : OpenSSL accepte-t-il cette signature ?
    pk = os.path.join(tmp, "pub.der"); m = os.path.join(tmp, "m.bin"); s = os.path.join(tmp, "s.bin")
    open(pk, "wb").write(bytes.fromhex("302a300506032b6570032100") + pub)
    open(m, "wb").write(msg); open(s, "wb").write(sig)
    return run("openssl", "pkeyutl", "-verify", "-pubin", "-inkey", pk, "-keyform", "DER", "-rawin", "-in", m, "-sigfile", s).returncode == 0

def hx(b): return b.hex() or "-"        # "-" = message vide (evite deux espaces de suite)

def ed(pk, sig, msg, expected): out.append(f"ed {pk.hex()} {sig.hex()} {hx(msg)} {expected}")

# RFC 8032, vecteur 1 (message vide)
ed(bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),
   bytes.fromhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
   b"", 1)

lengths = [1, 2, 3, 31, 32, 33, 63, 64, 65, 111, 112, 113, 127, 128, 129, 200, 255, 256, 1000, 4096, 20000, 100000]
mutations = 0
for n in lengths:
    pem, pk = keypair()
    msg = rnd.randbytes(n)
    sig = sign(pem, msg)
    ed(pk, sig, msg, 1)
    assert openssl_accepts(pk, msg, sig)
    # un bit change dans R, dans S, dans le message, dans la cle, signature d'un autre message
    for part in ("R", "S", "msg", "pk", "other"):
        s2, m2, p2 = bytearray(sig), bytearray(msg), bytearray(pk)
        if part == "R": s2[rnd.randrange(32)] ^= 1 << rnd.randrange(8)
        elif part == "S": s2[32 + rnd.randrange(31)] ^= 1 << rnd.randrange(8)
        elif part == "msg":
            if not m2: continue
            m2[rnd.randrange(len(m2))] ^= 1 << rnd.randrange(8)
        elif part == "pk": p2[rnd.randrange(32)] ^= 1 << rnd.randrange(8)
        else: m2 = bytearray(msg + b"x")
        assert not openssl_accepts(bytes(p2), bytes(m2), bytes(s2)), f"openssl accepte une mutation ({part}, n={n})"
        ed(bytes(p2), bytes(s2), bytes(m2), 0)
        mutations += 1
    # S non canonique : S + L (meme classe mod L, encodable sur 32 octets) doit etre refuse
    S = int.from_bytes(sig[32:], "little")
    if S + Lq < 2**256:
        ed(pk, sig[:32] + (S + Lq).to_bytes(32, "little"), msg, 0)
    # S = L exactement
    ed(pk, sig[:32] + Lq.to_bytes(32, "little"), msg, 0)

# cles d'ordre faible : doivent etre refusees quelle que soit la signature
def decode(enc):
    y = int.from_bytes(enc, "little"); sign = y >> 255; y &= (1 << 255) - 1
    if y >= P: return None
    d = (-121665 * pow(121666, P - 2, P)) % P
    x2 = ((y*y - 1) * pow(d*y*y + 1, P - 2, P)) % P
    x = pow(x2, (P + 3) // 8, P)
    if (x*x - x2) % P: x = x * pow(2, (P - 1) // 4, P) % P
    if (x*x - x2) % P: return None
    if x % 2 != sign: x = P - x
    return (x, y)

def pt_add(a, b):
    d = (-121665 * pow(121666, P - 2, P)) % P
    (x1, y1), (x2, y2) = a, b
    t = d * x1 * x2 * y1 * y2 % P
    return ((x1*y2 + x2*y1) * pow(1 + t, P - 2, P) % P, (y1*y2 + x1*x2) * pow(1 - t, P - 2, P) % P)

small = ["0100000000000000000000000000000000000000000000000000000000000000",
         "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
         "0000000000000000000000000000000000000000000000000000000000000080",
         "0000000000000000000000000000000000000000000000000000000000000000",
         "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
         "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
         "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
         "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85"]
for enc in small:
    p = decode(bytes.fromhex(enc))
    assert p is not None, f"{enc} n'est pas un point de la courbe"
    q = p
    for _ in range(3): q = pt_add(q, q)                      # 8 * P
    assert q == (0, 1), f"{enc} n'est pas d'ordre faible"
    for _ in range(3):
        ed(bytes.fromhex(enc), rnd.randbytes(32) + (rnd.randrange(Lq)).to_bytes(32, "little"), rnd.randbytes(rnd.randrange(60)), 0)
    ed(bytes.fromhex(enc), bytes(64), b"", 0)

# encodages non canoniques de y (y >= p) : refuses
for y in (P, P + 1, P + 2, 2**255 - 1, P + 18):
    ed(y.to_bytes(32, "little"), rnd.randbytes(32) + (rnd.randrange(Lq)).to_bytes(32, "little"), b"msg", 0)
# cle qui n'est pas sur la courbe
for _ in range(40):
    while True:
        enc = rnd.randbytes(32)
        if decode(enc) is None and int.from_bytes(enc, "little") & ((1 << 255) - 1) < P: break
    ed(enc, rnd.randbytes(32) + (rnd.randrange(Lq)).to_bytes(32, "little"), b"hors courbe", 0)

# SHA-512 : toutes les longueurs de 0 a 300 puis quelques grandes
for n in list(range(0, 301)) + [1000, 4095, 4096, 4097, 100000]:
    m = rnd.randbytes(n)
    out.append(f"sha {hx(m)} {hashlib.sha512(m).hexdigest()}")
out.append(f"sha {(b'a' * 1000000).hex()} {hashlib.sha512(b'a' * 1000000).hexdigest()}")

sys.stdout.write("\n".join(out) + "\n")
print(f"{sum(1 for l in out if l.startswith('ed '))} cas Ed25519 (dont {mutations} mutations), "
      f"{sum(1 for l in out if l.startswith('sha '))} cas SHA-512", file=sys.stderr)
