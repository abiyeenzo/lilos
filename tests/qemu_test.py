#!/usr/bin/env python3
"""Demarre l'image dans QEMU, tape des commandes au clavier PS/2 via le
moniteur QEMU et verifie la sortie sur le port serie."""
import os, re, socket, subprocess, sys, tempfile, time

IMG = sys.argv[1] if len(sys.argv) > 1 else "build/lilos.img"
QEMU = os.environ.get("QEMU", "qemu-system-i386")
KEYS = {" ": "spc", "\n": "ret", "-": "minus", ".": "dot"}


class Vm:
    def __init__(self, cpu=None):
        self.dir = tempfile.mkdtemp()
        self.log = os.path.join(self.dir, "serial.log")
        self.mon = os.path.join(self.dir, "mon")
        self.proc = subprocess.Popen(
            [QEMU, "-drive", f"format=raw,file={IMG}", "-display", "none",
             "-serial", f"file:{self.log}", "-monitor", f"unix:{self.mon},server,nowait",
             "-no-reboot"] + (["-cpu", cpu] if cpu else []),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(self.mon):
                break
            time.sleep(0.1)
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(self.mon)
        self.sock.settimeout(0.05)
        self.drain()

    def drain(self):
        try:
            while self.sock.recv(4096):
                pass
        except (socket.timeout, OSError):
            pass

    def text(self):
        try:
            return open(self.log, errors="replace").read()
        except FileNotFoundError:
            return ""

    def wait(self, pattern, since=0, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pattern, self.text()[since:])
            if m:
                return True
            time.sleep(0.1)
        return False

    def type(self, line):
        for c in line + "\n":
            self.sock.sendall(f"sendkey {KEYS.get(c, c)}\n".encode())
            time.sleep(0.06)
            self.drain()

    def close(self):
        self.proc.kill()
        self.proc.wait()


failures = []


def check(vm, cmd, pattern, timeout=10):
    since = len(vm.text())
    vm.type(cmd)
    ok = vm.wait(pattern, since, timeout)
    print(("ok     " if ok else "ECHEC  ") + f"{cmd!r} -> /{pattern}/")
    if not ok:
        failures.append(cmd)
        print("--- sortie ---\n" + vm.text()[since:] + "\n--------------")


def boot(cpu=None):
    vm = Vm(cpu)
    if not vm.wait(r"LilOS pret", 0, 20):
        print("ECHEC  demarrage\n" + vm.text())
        vm.close()
        sys.exit(1)
    return vm


def fresh(cpu, cmd, pattern, timeout=15):
    """Un test par demarrage : les exceptions arretent la machine."""
    vm = boot(cpu)
    print(f"[cpu={cpu or 'defaut'}]", end=" ")
    check(vm, cmd, pattern, timeout)
    vm.close()


vm = boot()
assert "PANIC" not in vm.text() and "EXCEPTION" not in vm.text()
check(vm, "version", r"LilOS 0\.2\.0 \(x86")
check(vm, "echo salut a tous", r"\nsalut a tous\r?\n")
check(vm, "memtest", r"memtest : OK")
check(vm, "mem", r"heap\s+: \d+ Ko utilises")
check(vm, "hexdump 0x10000 16", r"00010000  ")
check(vm, "hexdump 0x0 16", r"hors de la zone mappee")
check(vm, "hexdump 0xfffffff0 64", r"hors de la zone mappee")
check(vm, "hexdump zzz", r"usage: hexdump")
check(vm, "uptime", r"\d+ h \d+ min \d+ s")
check(vm, "date", r"20\d\d-\d\d-\d\d \d\d:\d\d:\d\d")
check(vm, "cpuid", r"vendeur : \w+")
check(vm, "pasunecommande", r"commande inconnue: pasunecommande")
check(vm, "mem", r"PAE, identite sur 16 Mo, NX (actif|indisponible), W\^X actif")
vm.close()

fault = r"EXCEPTION 14: Page fault[\s\S]*acces: "
fresh(None, "nulltest", fault + r"page absente, lecture")
fresh(None, "stacktest", r"DOUBLE FAULT")
fresh(None, "panic", r"KERNEL PANIC: declenche depuis le shell")

# W^X sans NX (Pentium III : PAE mais pas de bit NX)
fresh("pentium3", "protect text", fault + r"protection, ecriture")
fresh("pentium3", "protect rodata", fault + r"protection, ecriture")
fresh("pentium3", "protect stack", r"NX indisponible")
fresh("pentium3", "mem", r"NX indisponible")

# W^X complet avec NX
fresh("max", "mem", r"NX actif")
fresh("max", "protect text", fault + r"protection, ecriture")
fresh("max", "protect rodata", fault + r"protection, ecriture")
fresh("max", "protect stack", fault + r"protection, execution")
fresh("max", "protect heap", fault + r"protection, execution")
fresh("max", "protect data", fault + r"protection, execution")
fresh("max", "stacktest", r"DOUBLE FAULT")

if failures:
    print(f"{len(failures)} test(s) en echec")
    sys.exit(1)
print("tous les tests passent")
