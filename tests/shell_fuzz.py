#!/usr/bin/env python3
"""Fuzz du shell : commandes et touches aleatoires tapees au clavier PS/2.
Echoue si le kernel panique, leve une exception ou ne repond plus."""
import random, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
import vm as vmmod

seed = int(sys.argv[1]) if len(sys.argv) > 1 else random.randrange(1 << 30)
rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 120
vmmod.IMG = sys.argv[3] if len(sys.argv) > 3 else "build/lilos.img"
rng = random.Random(seed)
print(f"seed={seed} rounds={rounds}")

NUMS = ["0", "1", "16", "64", "512", "513", "4096", "0x0", "0x1000", "0xfff", "0x10000",
        "0x7efff", "0x7f000", "0x7ffff", "0x80000", "0xfffff0", "0xffffff", "0x1000000",
        "0xffffffff", "0x100000000", "4294967295", "4294967296", "-1", "0x", "0xg", "99999999999",
        "007", "0X1F", ""]
SAFE = ["ls", "id", "afficher", "supprimer", "copier", "ecrire", "ajouter", "calculer", "aide", "cat", "write", "clear", "echo", "version", "uptime", "date", "mem", "cpuid", "color", "sleep",
        "hexdump", "memtest", "history", "protect", "nope", "ECHO", "he lp"]
ALPHA = "abcdefghijklmnopqrstuvwxyz0123456789 -.=/;,'éèàçù@#{}"


PROGS = ["bonjour", "moi", "lire", "noter", "noyau", "ecrase", "inconnu", "", "../x"]


def rand_cmd():
    kind = rng.random()
    if kind < 0.18:
        p = rng.choice(PROGS)
        a = rng.choice(["", "a", "f1 texte", "x" * rng.randint(1, 70), "a b c d e f g h i j", "é"])
        return f"lancer {p} {a}"
    if kind < 0.26:
        return rng.choice(["id", "utilisateurs", "programmes", "droits f1 64", "droits f1 99", "droits",
                           "proprietaire f1 alice", "proprietaire f1 zzz", "supprimeruser zzz", "creeruser alice"])
    if kind < 0.45:
        if rng.random() < 0.35:
            n = rng.choice(["a", "b.txt", "x-y_z", "..", "bad/n", "A" * 23, "A" * 24, "", "f" * 5])
            op = rng.choice(["write", "append", "cat", "rm", "ls", "cp"])
            return f"{op} {n} " + rng.choice(["", "texte", "x" * rng.randint(1, 110), n])
        if rng.random() < 0.3:
            return "calc " + rng.choice(NUMS[:20]) + " " + rng.choice(["+", "-", "*", "/", "%", "x", "++"]) + " " + rng.choice(NUMS[:20])
        c = rng.choice(["hexdump", "color", "sleep", "echo", "memtest", "mem", "cpuid", "date", "history"])
        if c == "sleep":
            return "sleep " + rng.choice(["0", "1", "20", "50", "-1", "x", "600001", "99999999999"])
        if c == "protect":
            return "protect " + rng.choice(["text", "stack"])
        args = " ".join(rng.choice(NUMS) for _ in range(rng.randint(0, 4)))
        return f"{c} {args}"
    if kind < 0.65:
        return rng.choice(SAFE) + " " + " ".join(rng.choice(ALPHA) * rng.randint(1, 6) for _ in range(rng.randint(0, 20)))
    if kind < 0.8:
        return "".join(rng.choice(ALPHA) for _ in range(rng.randint(100, 260)))   # depasse LINE_MAX
    if kind < 0.9:
        # edition : backspaces, historique, Ctrl+L au milieu d'une ligne
        parts = []
        for _ in range(rng.randint(3, 25)):
            parts.append(rng.choice(["\b", "\b", "\x01", "\x02", "\x0c", rng.choice(ALPHA)]))
        return "".join(parts)
    return " " * rng.randint(1, 40) + "echo" + " " * rng.randint(1, 40) + "x"


vm = vmmod.Vm()
if not vm.wait(r"LilOS prêt", 0, 20):
    print("ECHEC demarrage")
    sys.exit(1)
vmmod.provision(vm)
vmmod.login(vm)

history = []
fail = None
for i in range(rounds):
    cmd = rand_cmd()
    history.append(cmd)
    vm.type(cmd, delay=0.02)
    mark = f"MARK{i}X"
    since = len(vm.text())
    vm.type("echo " + mark, delay=0.02)
    # la sortie contient la saisie ET la reponse : on attend la 2e occurrence
    if not vm.wait(rf"{mark}[\s\S]*{mark}", since, 20):
        fail = f"plus de reponse apres {cmd!r}"
        break
    out = vm.text()
    if "PANIC" in out or "EXCEPTION" in out or "DOUBLE FAULT" in out:
        fail = f"crash apres {cmd!r}"
        break

print(f"{len(history)} commandes envoyees")
if fail:
    print("ECHEC:", fail)
    print("dernieres commandes:", history[-5:])
    print("--- fin de la sortie serie ---")
    print(vm.text()[-1500:])
    vm.close()
    sys.exit(1)
vm.close()
print("fuzz du shell: aucun crash")
