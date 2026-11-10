#!/usr/bin/env python3
"""Attaque reseau en conditions reelles : ce script EST le reseau de la VM
(-netdev socket) et lui envoie des paquets hostiles. Verifie les reponses
attendues, l'absence de reponse aux paquets invalides, et que le systeme tient."""
import os, random, re, socket, struct, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm as vmmod

vmmod.IMG = sys.argv[1] if len(sys.argv) > 1 else "build/lilos.img"
PORT = 23000 + os.getpid() % 2000
GUEST_MAC = bytes.fromhex("525400123456")
PEER_MAC = bytes.fromhex("525500000099")
GW_MAC = bytes.fromhex("525500000002")
GUEST_IP, PEER_IP, GW_IP, DNS_IP = "10.0.2.15", "10.0.2.99", "10.0.2.2", "10.0.2.3"
ip4 = lambda s: bytes(int(x) for x in s.split("."))

def csum(b):
    if len(b) % 2: b += b"\0"
    s = sum(struct.unpack("!%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

def eth(dst, src, typ, payload): return dst + src + struct.pack("!H", typ) + payload
def arp(op, smac, sip, tmac, tip):
    return struct.pack("!HHBBH", 1, 0x0800, 6, 4, op) + smac + ip4(sip) + tmac + ip4(tip)
def ipv4(src, dst, proto, payload, flags=0x4000, fix=True):
    h = struct.pack("!BBHHHBBH", 0x45, 0, 20 + len(payload), 1, flags, 64, proto, 0) + ip4(src) + ip4(dst)
    if fix: h = h[:10] + struct.pack("!H", csum(h)) + h[12:]
    return h + payload
def icmp_echo(i, seq, data=b"abcdefgh" * 4, fix=True):
    m = struct.pack("!BBHHH", 8, 0, 0, i, seq) + data
    return m[:2] + struct.pack("!H", csum(m) if fix else 0x1234) + m[4:]
def udp(sp, dp, data, src, dst):
    seg = struct.pack("!HHHH", sp, dp, 8 + len(data), 0) + data
    c = csum(ip4(src) + ip4(dst) + struct.pack("!BBH", 0, 17, len(seg)) + seg) or 0xFFFF
    return seg[:6] + struct.pack("!H", c) + seg[8:]

class Net:
    """Cote reseau : recoit les trames du guest, en envoie."""
    def __init__(self):
        self.frames, self.lock = [], threading.Lock()
        self.sock = None
        self.hook = None
    def connect(self):
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", PORT)); break
            except OSError: time.sleep(0.1)
        threading.Thread(target=self.reader, daemon=True).start()
    def reader(self):
        buf = b""
        while True:
            try: d = self.sock.recv(65536)
            except OSError: return
            if not d: return
            buf += d
            while len(buf) >= 4:
                n = struct.unpack("!I", buf[:4])[0]
                if len(buf) < 4 + n: break
                f, buf = buf[4:4 + n], buf[4 + n:]
                with self.lock: self.frames.append(f)
                if self.hook: self.hook(f)
    def send(self, f):
        try: self.sock.sendall(struct.pack("!I", len(f)) + f)
        except OSError: pass
    def take(self, pred, timeout=3):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                for i, f in enumerate(self.frames):
                    if pred(f): return self.frames.pop(i)
            time.sleep(0.02)
        return None
    def clear(self):
        with self.lock: self.frames.clear()

failures = []
def check(name, ok, extra=""):
    print(("ok     " if ok else "ECHEC  ") + name + (" " + extra if extra and not ok else ""))
    if not ok: failures.append(name)

vm = vmmod.Vm(extra=["-netdev", f"socket,id=n0,listen=127.0.0.1:{PORT}",
                     "-device", "rtl8139,netdev=n0,mac=52:54:00:12:34:56"])
net = Net(); net.connect()
vm.wait(r"LilOS prêt", 0, 20)
vmmod.provision(vm); vmmod.login(vm)

def alive():
    since = len(vm.text()); vm.type("id", delay=0.03)
    return vm.wait(r"uid=0\(root\)", since, 15)

is_arp_reply = lambda f: f[12:14] == b"\x08\x06" and f[20:22] == b"\x00\x02"
is_echo_reply = lambda f: f[12:14] == b"\x08\x00" and f[23] == 1 and f[34] == 0

# 1. ARP : le guest repond a une requete qui le concerne
net.send(eth(b"\xff" * 6, PEER_MAC, 0x0806, arp(1, PEER_MAC, PEER_IP, b"\0" * 6, GUEST_IP)))
r = net.take(is_arp_reply)
check("ARP : reponse a une requete legitime", r is not None and r[22:28] == GUEST_MAC and r[28:32] == ip4(GUEST_IP))
net.clear()

# 2. ARP : requete pour une autre machine, pas de reponse
net.send(eth(b"\xff" * 6, PEER_MAC, 0x0806, arp(1, PEER_MAC, PEER_IP, b"\0" * 6, "10.0.2.77")))
check("ARP : silence pour une requete destinee a un tiers", net.take(is_arp_reply, 1) is None)

# 3. ping : reponse correcte
net.clear()
net.send(eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, GUEST_IP, 1, icmp_echo(7, 1))))
r = net.take(is_echo_reply)
ok = r is not None and csum(r[14:34]) == 0 and csum(r[34:]) == 0 and r[38:42] == struct.pack("!HH", 7, 1)
check("ICMP : reponse a un ping, sommes valides", ok)

# 4. attaques sans reponse attendue
def silent(name, frame):
    net.clear(); net.send(frame)
    check("silence : " + name, net.take(lambda f: f[12:14] == b"\x08\x00" and f[23] == 1, 1) is None)
silent("ping en diffusion (smurf)", eth(b"\xff" * 6, PEER_MAC, 0x0800, ipv4(PEER_IP, "255.255.255.255", 1, icmp_echo(1, 2))))
silent("ping sur l'adresse de diffusion du reseau", eth(b"\xff" * 6, PEER_MAC, 0x0800, ipv4(PEER_IP, "10.0.2.255", 1, icmp_echo(1, 3))))
silent("somme ICMP fausse", eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, GUEST_IP, 1, icmp_echo(1, 4, fix=False))))
silent("somme IP fausse", eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, GUEST_IP, 1, icmp_echo(1, 5), fix=False)))
silent("fragment IP", eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, GUEST_IP, 1, icmp_echo(1, 6), flags=0x2000)))
silent("source multicast", eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4("224.0.0.1", GUEST_IP, 1, icmp_echo(1, 7))))
silent("ping destine a un tiers", eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, "10.0.2.44", 1, icmp_echo(1, 8))))
silent("IP source == adresse du guest (reflexion)", eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(GUEST_IP, GUEST_IP, 1, icmp_echo(1, 9))))

# 5. empoisonnement ARP : reponses non sollicitees ignorees
net.send(eth(GUEST_MAC, PEER_MAC, 0x0806, arp(2, PEER_MAC, "10.0.2.66", GUEST_MAC, GUEST_IP)))
net.send(eth(GUEST_MAC, PEER_MAC, 0x0806, arp(2, PEER_MAC, GW_IP, GUEST_MAC, GUEST_IP)))
time.sleep(0.5)
since = len(vm.text()); vm.type("arp", delay=0.03); vm.wait(r"\w+# ", since, 10)
out = vm.text()[since:]
check("ARP : reponses non sollicitees non apprises", "10.0.2.66" not in out and GW_MAC.hex() not in out.replace(":", ""), out[-200:])

# 6. inondation de pings : le guest repond puis reste utilisable
net.clear()
for i in range(3000):
    net.send(eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, GUEST_IP, 1, icmp_echo(9, i, data=bytes([i % 256]) * 56))))
time.sleep(2)
with net.lock: replies = sum(1 for f in net.frames if is_echo_reply(f))
check(f"inondation de 3000 pings : {replies} reponses, guest vivant", replies > 100 and alive())

# 7. tailles de trames extremes, dont des trames plus longues que le maximum Ethernet
def rx_alive():
    net.clear()
    net.send(eth(b"\xff" * 6, PEER_MAC, 0x0806, arp(1, PEER_MAC, PEER_IP, b"\0" * 6, GUEST_IP)))
    return net.take(is_arp_reply, 3) is not None
for size in (14, 15, 33, 34, 59, 60, 61, 1514, 1518, 1519, 1600, 2000, 4000, 9000):
    net.send(GUEST_MAC + os.urandom(max(0, size - 6)))
    time.sleep(0.2)
check("trames de tailles extremes : guest vivant", alive())
check("trames surdimensionnees : la reception du pilote reste vivante", rx_alive())

# 8. fuzz : trames valides mutees
rnd = random.Random(1234)
seeds = [
    eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(PEER_IP, GUEST_IP, 1, icmp_echo(1, 1))),
    eth(b"\xff" * 6, PEER_MAC, 0x0806, arp(1, PEER_MAC, PEER_IP, b"\0" * 6, GUEST_IP)),
    eth(GUEST_MAC, PEER_MAC, 0x0800, ipv4(DNS_IP, GUEST_IP, 17, udp(53, 40000, b"\x12\x34" * 40, DNS_IP, GUEST_IP))),
]
for _ in range(6000):
    f = bytearray(rnd.choice(seeds))
    for _ in range(rnd.randint(1, 5)): f[rnd.randrange(len(f))] = rnd.randrange(256)
    if rnd.random() < 0.2: f = f[: 1 + rnd.randrange(len(f))]      # jamais 0 octet : QEMU y verrait une fermeture de connexion
    net.send(bytes(f))
time.sleep(1)
check("6000 trames mutees : guest vivant", alive())
check("apres le fuzz : la reception du pilote reste vivante", rx_alive())

# 9. DNS : reponse falsifiee refusee, bonne reponse acceptee
answered = {"bad": False, "good": False}
def dns_server(f):
    if f[12:14] == b"\x08\x06" and f[20:22] == b"\x00\x01" and f[38:42] == ip4(DNS_IP):          # ARP pour le DNS
        net.send(eth(GUEST_MAC, GW_MAC, 0x0806, arp(2, GW_MAC, DNS_IP, GUEST_MAC, GUEST_IP)))
    elif f[12:14] == b"\x08\x00" and f[23] == 17 and f[30:34] == ip4(DNS_IP) and f[36:38] == b"\x00\x35":
        sport = struct.unpack("!H", f[34:36])[0]
        q = f[42:]
        tid = q[:2]
        question = q[12:]
        end = question.index(b"\0") + 5
        question = question[:end]
        def resp(tid_, ip, dst_ip=GUEST_IP, srcip=DNS_IP, psport=53):
            body = tid_ + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00" + question + b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04" + ip4(ip)
            return eth(GUEST_MAC, GW_MAC, 0x0800, ipv4(srcip, dst_ip, 17, udp(psport, sport, body, srcip, dst_ip)))
        if not answered["bad"]:
            answered["bad"] = True
            net.send(resp(bytes([tid[0] ^ 1, tid[1]]), "6.6.6.6"))                  # mauvais identifiant
            net.send(resp(tid, "7.7.7.7", srcip="10.0.2.77"))                       # mauvais serveur
            net.send(resp(tid, "8.8.8.8", psport=5353))                             # mauvais port source
            time.sleep(0.3)
            net.send(resp(tid, "93.184.216.34"))                                    # la bonne
def traced(f):
    if os.environ.get("NET_TRACE"):
        print("  guest->reseau:", f[12:14].hex(), "len", len(f), "arp" if f[12:14]==b"\x08\x06" else ("udp" if f[23]==17 else f[23]), "dst", f[0:6].hex(), flush=True)
    dns_server(f)
net.hook = traced
if os.environ.get("NET_TRACE"):
    since = len(vm.text()); vm.type("arp", delay=0.03); vm.wait(r"\w+# ", since, 10); print(vm.text()[since:], flush=True)
since = len(vm.text()); vm.type("dns exemple.test", delay=0.03)
ok = vm.wait(r"exemple\.test : 93\.184\.216\.34", since, 20)
out = vm.text()[since:]
check("DNS : seules la bonne reponse (id, serveur, port) est acceptee", ok and "6.6.6.6" not in out and "7.7.7.7" not in out and "8.8.8.8" not in out, out[-300:])
net.hook = None

check("systeme intact a la fin", alive() and "PANIC" not in vm.text() and "EXCEPTION" not in vm.text())
vm.close()
if failures:
    print(f"{len(failures)} test(s) en echec : {failures}"); sys.exit(1)
print("attaque reseau : aucune reponse indue, aucun crash")
