#!/usr/bin/env python3
"""Attaques contre le noyau depuis un processus utilisateur non privilegie.
Chaque attaque doit etre refusee (REFUSE) ou tuer le processus sans que le
systeme ne tombe : apres chacune, un programme sain doit encore tourner."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm as vmmod

vmmod.IMG = sys.argv[1] if len(sys.argv) > 1 else "build/lilos.img"

REFUSE = r"REFUSE"
KILLED = r"processus attaque \(pid \d+\) tué"
SURVIVE = r"FIN de l'attaque"

# numero -> (resultat attendu, description)
ATTACKS = {
    0: (REFUSE, "write depuis la memoire du noyau"), 1: (REFUSE, "fread vers la memoire du noyau"),
    2: (REFUSE, "tampon a cheval sur la fin de l'espace"), 3: (REFUSE, "debordement d'adresse"),
    4: (REFUSE, "longueur 0x7FFFFFFF"), 5: (REFUSE, "nom de fichier dans le noyau"),
    6: (REFUSE, "nom sans terminateur"), 7: (REFUSE, "numeros d'appel inconnus"),
    8: (REFUSE, "lecture de 1 octet"), 9: (REFUSE, "mauvais descripteur"),
    10: (REFUSE, "read dans son propre code (lecture seule)"), 11: (REFUSE, "fwrite depuis le noyau"),
    12: (REFUSE, "sleep enorme"), 13: (REFUSE, "fwrite longueur enorme"),
    20: (KILLED, "cli"), 21: (KILLED, "hlt"), 22: (KILLED, "out"), 23: (KILLED, "lecture de CR3"),
    24: (KILLED, "lgdt"), 25: (KILLED, "rdmsr"), 26: (KILLED, "segment noyau dans DS"),
    27: (KILLED, "int 0x20"), 28: (KILLED, "int 14"), 29: (KILLED, "int 3"),
    30: (KILLED, "saut lointain vers le segment noyau"), 31: (KILLED, "division par zero"),
    32: (KILLED, "ud2"), 33: (KILLED, "trap flag"), 34: (KILLED, "saut dans le noyau"),
    35: (KILLED, "execution de donnees (NX)"), 36: (KILLED, "debordement de pile utilisateur"),
    37: (KILLED, "lecture a l'adresse 0"), 38: (KILLED, "wrmsr"),
    39: (r"ok rdtsc", "rdtsc (autorise)"),
    40: (r"DF=1[\s\S]*" + SURVIVE, "drapeau DF avant un appel systeme"),
    41: (r"NT=1", "drapeau NT avant un appel systeme"), 42: (r"AC=1", "drapeau AC avant un appel systeme"),
    43: (KILLED, "IOPL=3 ignore, cli toujours interdit"),
    44: (r"20000 appels", "20000 appels systeme"),
    45: (r"processus attaque|FIN de l'attaque", "esp invalide au moment d'un appel systeme"),
    46: (KILLED, "lecture du port clavier 0x60 (espionnage des mots de passe)"),
    47: (KILLED, "ecriture du port PCI 0xCF8"), 48: (KILLED, "ecriture du PIC"),
    49: (KILLED, "lecture du port serie"), 50: (KILLED, "lecture du timer"),
}

vm = vmmod.Vm(cpu="max")
vm.wait(r"LilOS prêt", 0, 20)
vmmod.provision(vm)
vmmod.login(vm, vmmod.USER, vmmod.USER_PW)

bad = []
for n, (expect, what) in ATTACKS.items():
    since = len(vm.text())
    vm.type(f"lancer attaque {n}", delay=0.04)
    ok = vm.wait(expect, since, 25)
    out = vm.text()[since:]
    breach = "BREACH" in out
    panic = "PANIC" in vm.text() or "EXCEPTION" in vm.text() or "DOUBLE FAULT" in vm.text()
    # le systeme tient-il toujours ?
    since2 = len(vm.text())
    vm.type("lancer moi", delay=0.04)
    alive = vm.wait(r"uid=1000 pid=\d+", since2, 25)
    status = "ok    " if ok and alive and not breach and not panic else "ECHEC "
    print(f"{status}{n:>3} {what}" + ("  [BREACH]" if breach else "") + ("  [panic noyau]" if panic else "") + ("" if alive else "  [systeme mort]") + ("" if ok else "  [reponse inattendue]"))
    if status == "ECHEC ":
        bad.append(n)
        print("   sortie:", out.strip().replace("\n", " | ")[:300])
    if panic or not alive:
        break

vm.close()
if bad:
    print(f"{len(bad)} attaque(s) a investiguer : {bad}")
    sys.exit(1)
print("aucune attaque n'a reussi : processus tues ou appels refuses, noyau intact")
