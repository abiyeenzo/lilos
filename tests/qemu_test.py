#!/usr/bin/env python3
"""Demarre l'image dans QEMU, tape des commandes au clavier PS/2 via le
moniteur QEMU et verifie la sortie sur le port serie."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm as vmmod
from vm import Vm

vmmod.IMG = sys.argv[1] if len(sys.argv) > 1 else "build/lilos.img"

failures = []


def check(vm, cmd, pattern, timeout=10):
    since = len(vm.text())
    vm.type(cmd)
    ok = vm.wait(pattern, since, timeout)
    print(("ok     " if ok else "ECHEC  ") + f"{cmd!r} -> /{pattern}/")
    if not ok:
        failures.append(cmd)
        print("--- sortie ---\n" + vm.text()[since:] + "\n--------------")


def check_raw(vm, keys, pattern, timeout=10):
    """Tape des touches brutes (retour arriere, fleches...) sans Entree ajoutee."""
    since = len(vm.text())
    vm.type(keys, enter=False)
    ok = vm.wait(pattern, since, timeout)
    print(("ok     " if ok else "ECHEC  ") + f"{keys!r} -> /{pattern}/")
    if not ok:
        failures.append(keys)
        print("--- sortie ---\n" + vm.text()[since:] + "\n--------------")


def boot(cpu=None):
    vm = Vm(cpu)
    if not vm.wait(r"LilOS prêt", 0, 20):
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
check(vm, "aide", r"ecrire[\s\S]*calculer")
check(vm, "help", r"ecrire[\s\S]*calculer")   # alias anglais
check(vm, "aide calculer", r"calculer : calcule")
check(vm, "ls", r"0 fichier\(s\), 0 octets")
check(vm, "write notes.txt bonjour le monde", r"lilos> ")
check(vm, "append notes.txt seconde ligne", r"lilos> ")
check(vm, "cat notes.txt", r"bonjour le monde\r?\nseconde ligne")
check(vm, "ls", r"notes.txt\s+31 octets[\s\S]*1 fichier\(s\), 31 octets")
check(vm, "cp notes.txt copie_2.txt", r"lilos> ")
check(vm, "cat copie_2.txt", r"seconde ligne")
check(vm, "rm notes.txt", r"lilos> ")
check(vm, "cat notes.txt", r"erreur : fichier introuvable")
check(vm, "rm notes.txt", r"erreur : fichier introuvable")
check(vm, "write bad/name x", r"erreur : nom invalide")
check(vm, "write " + "a" * 24 + " x", r"erreur : nom invalide")
check(vm, "write vide", r"lilos> ")
check(vm, "cat vide", r"lilos> ")
check(vm, "calc 6 * 7", r"\n42\r?\n")
check(vm, "calc -5 - 10", r"\n-15\r?\n")
check(vm, "calc 17 % 5", r"\n2\r?\n")
check(vm, "calc 1 / 0", r"division par zéro")
check(vm, "calc 5 % 0", r"division par zéro")
check(vm, "calc 2147483647 + 1", r"débordement")
check(vm, "calc -2147483648 - 1", r"débordement")
check(vm, "calc 65536 * 65536", r"débordement")
check(vm, "calc -2147483648 / -1", r"débordement")
check(vm, "calc 2147483648 + 0", r"usage : calculer")
check(vm, "calc 1 x 2", r"opérateur inconnu")
check(vm, "calc 1 ++ 2", r"usage : calculer")
check(vm, "mem", r"heap\s+: \d+ Ko utilisés")
check(vm, "hexdump 0x10000 16", r"00010000  ")
check(vm, "hexdump 0x0 16", r"hors de la zone mappée")
check(vm, "hexdump 0xfffffff0 64", r"hors de la zone mappée")
check(vm, "hexdump 0x7ffff 4", r"hors de la zone mappée")     # page de garde de la pile
check(vm, "hexdump 0x7efff 2", r"hors de la zone mappée")     # chevauche la garde
check(vm, "hexdump 0xfffff0 32", r"hors de la zone mappée")   # depasse 16 Mo
check(vm, "hexdump 0x80000 16", r"00080000  ")                # pile mappée
check(vm, "hexdump zzz", r"usage : hexdump")
check(vm, "uptime", r"\d+ h \d+ min \d+ s")
check(vm, "date", r"20\d\d-\d\d-\d\d \d\d:\d\d:\d\d")
check(vm, "cpuid", r"fabricant : \w+")
check(vm, "echo azerty qsdf wxcvbn", r"\nazerty qsdf wxcvbn\r?\n")
check(vm, "echo éèàçù", r"\néèàçù\r?\n")
check(vm, "echo 1234567890 &\"'(-_)=", r"\n1234567890 &\"'\(-_\)=\r?\n")
check(vm, "echo ABCDEF °+£¨%µ§?./", r"\nABCDEF °\+£¨%µ§\?\./\r?\n")
check(vm, "echo @#{[|`\\^]}~", r"\n@#\{\[\|`\\\^\]\}~\r?\n")
check(vm, "ecrire accents.txt café crème à côté".replace("ô", "o"), r"lilos> ")
check(vm, "afficher accents.txt", r"café crème à coté")
check_raw(vm, "echo aé\bb\n", r"\nab\r?\n")        # retour arriere sur un caractere accentue
check_raw(vm, "echo éé\b\bx\n", r"\nx\r?\n")
check(vm, "echo hé", r"\nhé\r?\n")
check_raw(vm, "\x01\n", r"\nhé\r?\n")             # fleche haut : historique avec accent
check(vm, "pasunecommande", r"commande inconnue : pasunecommande")
check(vm, "mem", r"PAE, identité sur 16 Mo, NX (actif|indisponible), W\^X actif")
vm.close()

fault = r"EXCEPTION 14 : Défaut de page[\s\S]*accès : "
fresh(None, "nulltest", fault + r"page absente, lecture")
fresh(None, "stacktest", r"DOUBLE FAULT")
fresh(None, "panic", r"KERNEL PANIC : déclenché depuis le shell")

# W^X sans NX (Pentium III : PAE mais pas de bit NX)
fresh("pentium3", "protect text", fault + r"protection, écriture")
fresh("pentium3", "protect rodata", fault + r"protection, écriture")
fresh("pentium3", "protect stack", r"NX indisponible")
fresh("pentium3", "mem", r"NX indisponible")

# W^X complet avec NX
fresh("max", "mem", r"NX actif")
fresh("max", "protect text", fault + r"protection, écriture")
fresh("max", "protect rodata", fault + r"protection, écriture")
fresh("max", "protect stack", fault + r"protection, exécution")
fresh("max", "protect heap", fault + r"protection, exécution")
fresh("max", "protect data", fault + r"protection, exécution")
fresh("max", "stacktest", r"DOUBLE FAULT")

if failures:
    print(f"{len(failures)} test(s) en echec")
    sys.exit(1)
print("tous les tests passent")
