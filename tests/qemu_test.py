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


def boot(cpu=None, user="root", pw=vmmod.ROOT_PW, net=False):
    vm = Vm(cpu, net)
    if not vm.wait(r"LilOS prêt", 0, 20):
        print("ECHEC  demarrage\n" + vm.text())
        vm.close()
        sys.exit(1)
    vmmod.provision(vm)
    vmmod.login(vm, user, pw)
    return vm


def fresh(cpu, cmd, pattern, timeout=15):
    """Un test par demarrage : les exceptions arretent la machine."""
    vm = boot(cpu)
    print(f"[cpu={cpu or 'defaut'}]", end=" ")
    check(vm, cmd, pattern, timeout)
    vm.close()


vm = boot()
assert "PANIC" not in vm.text() and "EXCEPTION" not in vm.text()
check(vm, "version", r"LilOS 0\.3\.0 \(x86")
check(vm, "echo salut a tous", r"\nsalut a tous\r?\n")
check(vm, "memtest", r"memtest : OK")
check(vm, "aide", r"ecrire[\s\S]*calculer")
check(vm, "help", r"ecrire[\s\S]*calculer")   # alias anglais
check(vm, "aide calculer", r"calculer : calcule")
check(vm, "ls", r"0 fichier\(s\), 0 octets")
check(vm, "write notes.txt bonjour le monde", vmmod.PROMPT)
check(vm, "append notes.txt seconde ligne", vmmod.PROMPT)
check(vm, "cat notes.txt", r"bonjour le monde\r?\nseconde ligne")
check(vm, "ls", r"notes.txt\s+31 octets[\s\S]*1 fichier\(s\), 31 octets")
check(vm, "cp notes.txt copie_2.txt", vmmod.PROMPT)
check(vm, "cat copie_2.txt", r"seconde ligne")
check(vm, "rm notes.txt", vmmod.PROMPT)
check(vm, "cat notes.txt", r"erreur : fichier introuvable")
check(vm, "rm notes.txt", r"erreur : fichier introuvable")
check(vm, "write bad/name x", r"erreur : nom invalide")
check(vm, "write " + "a" * 24 + " x", r"erreur : nom invalide")
check(vm, "write vide", vmmod.PROMPT)
check(vm, "cat vide", vmmod.PROMPT)
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
check(vm, "hexdump 0x1fffff0 32", r"hors de la zone mappée")  # depasse 32 Mo
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
check(vm, "ecrire accents.txt café crème à côté".replace("ô", "o"), vmmod.PROMPT)
check(vm, "afficher accents.txt", r"café crème à coté")
check_raw(vm, "echo aé\bb\n", r"\nab\r?\n")        # retour arriere sur un caractere accentue
check_raw(vm, "echo éé\b\bx\n", r"\nx\r?\n")
check(vm, "echo hé", r"\nhé\r?\n")
check_raw(vm, "\x01\n", r"\nhé\r?\n")             # fleche haut : historique avec accent
check(vm, "pasunecommande", r"commande inconnue : pasunecommande")
check(vm, "mem", r"PAE, identité sur 32 Mo, NX (actif|indisponible), W\^X actif")
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

# ---------------------------------------------------------------------------
# Multi-utilisateur et mode utilisateur
# ---------------------------------------------------------------------------

def ask(vm, cmd, secret_prompt, secret, final, timeout=15):
    """Lance une commande qui demande un mot de passe, le tape, verifie la sortie finale."""
    since = len(vm.text())
    vm.type(cmd)
    ok = vm.wait(secret_prompt, since, 10)
    if ok:
        vm.type(secret, delay=0.03)
        ok = vm.wait(final, since, timeout)
    print(("ok     " if ok else "ECHEC  ") + f"{cmd!r} + secret -> /{final}/")
    if not ok:
        failures.append(cmd)
        print("--- sortie ---\n" + vm.text()[since:] + "\n--------------")


# comptes : alice (utilisateur) cree au premier demarrage, root
vm = boot(user="alice", pw=vmmod.USER_PW)
check(vm, "id", r"uid=1000\(alice\)")
check(vm, "utilisateurs", r"root\s+uid 0[\s\S]*alice\s+uid 1000")
# privileges : un utilisateur normal ne touche ni au noyau ni aux comptes
check(vm, "hexdump 0x10000 16", r"permission refusée")
check(vm, "creeruser eve", r"permission refusée")
check(vm, "eteindre", r"permission refusée")
check(vm, "stacktest", r"permission refusée")
check(vm, "memoire", r"permission refusée")
check(vm, "supprimeruser alice", r"permission refusée")
ask(vm, "su", r"Mot de passe", "mauvais", r"su : mot de passe incorrect")
check(vm, "id", r"uid=1000\(alice\)")                       # toujours alice
# fichiers : proprietaire et droits
check(vm, "ecrire prive.txt contenu secret", vmmod.PROMPT)
check(vm, "ls", r"rw-r--\s+alice\s+prive\.txt")
check(vm, "droits prive.txt 60", vmmod.PROMPT)
check(vm, "ls", r"rw----\s+alice\s+prive\.txt")
check(vm, "droits prive.txt 9", r"usage : droits")
# programmes en mode utilisateur
check(vm, "programmes", r"bonjour[\s\S]*saisie")
check(vm, "lancer bonjour", r"Bonjour depuis le mode utilisateur \(ring 3\) !")
check(vm, "lancer moi un deux", r"uid=1000 pid=\d+ arguments=2 \[un\] \[deux\]")
check(vm, "lancer lire prive.txt", r"contenu secret")           # son propre fichier
check(vm, "lancer noter note.txt bonjour", r"écrit")
check(vm, "afficher note.txt", r"bonjour")
check(vm, "lancer lire absent.txt", r"lire : erreur -2")
check(vm, "lancer inconnu", r"programme inconnu")
# isolation : le processus ne peut ni lire le noyau ni ecrire dans son code
check(vm, "lancer noyau", r"processus noyau \(pid \d+\) tué : Défaut de page à l'adresse 00010000")
check(vm, "lancer ecrase", r"processus ecrase \(pid \d+\) tué : Défaut de page à l'adresse 40000100")
check(vm, "lancer moi", r"uid=1000")                             # le systeme tient toujours
since = len(vm.text())
vm.type("lancer saisie")
if vm.wait(r"Votre nom \? ", since, 10):
    vm.type("Zoé")
ok = vm.wait(r"Bonjour, Zoé !", since, 10)
print(("ok     " if ok else "ECHEC  ") + "'lancer saisie' (lecture clavier en mode utilisateur)")
if not ok: failures.append("saisie")
# Ctrl+C interrompt un processus qui boucle
since = len(vm.text())
vm.type("lancer boucle")
vm.wait(r"boucle infinie", since, 10)
vm.type("\x03", enter=False)
ok = vm.wait(r"processus interrompu \(Ctrl\+C\)", since, 10)
print(("ok     " if ok else "ECHEC  ") + "Ctrl+C sur un processus en boucle")
if not ok: failures.append("ctrl-c")
check(vm, "lancer moi", r"uid=1000")
vm.close()

# root : tout est permis, et su vers un autre compte
vm = boot()
check(vm, "id", r"uid=0\(root\)")
check(vm, "hexdump 0x10000 16", r"00010000  ")
ask(vm, "creeruser eve", r"Nouveau mot de passe", "evepass1", r"Confirmez")
vm.type("evepass1", delay=0.03)
check(vm, "utilisateurs", r"eve\s+uid 1001")
check(vm, "creeruser eve", r"ce compte existe déjà")
check(vm, "ecrire prive.txt confidentiel", vmmod.PROMPT)
check(vm, "droits prive.txt 60", vmmod.PROMPT)
check(vm, "su eve", vmmod.PROMPT)                                # root passe sans mot de passe
check(vm, "id", r"uid=1001\(eve\)")
check(vm, "afficher prive.txt", r"permission refusée")           # eve ne lit pas le fichier de root
check(vm, "lancer lire prive.txt", r"lire : erreur -6")          # meme depuis le mode utilisateur
check(vm, "ecrire prive.txt piratage", r"permission refusée")
check(vm, "supprimer prive.txt", r"permission refusée")
check(vm, "droits prive.txt 66", r"permission refusée")
check(vm, "proprietaire prive.txt eve", r"permission refusée")
check(vm, "deconnexion", vmmod.PROMPT)                            # retour a root
check(vm, "id", r"uid=0\(root\)")
check(vm, "afficher prive.txt", r"confidentiel")
check(vm, "proprietaire prive.txt eve", vmmod.PROMPT)
check(vm, "ls", r"rw----\s+eve\s+prive\.txt")
check(vm, "supprimeruser eve", r"compte supprimé")
check(vm, "ls", r"0 fichier\(s\)")                                # ses fichiers partent avec lui
check(vm, "supprimeruser root", r"suppression impossible")
vm.close()

# connexion : mauvais mot de passe refuse, bon mot de passe accepte, session fermee
vm = Vm()
vm.wait(r"LilOS prêt", 0, 20)
vmmod.provision(vm)
vmmod.login(vm, "root", "pasbon1", expect_prompt=False)
ok = vm.wait(r"Identifiant ou mot de passe incorrect", 0, 10)
vmmod.login(vm, "inconnu", "nimportequoi", expect_prompt=False)
vmmod.login(vm, "root", vmmod.ROOT_PW)
ok = ok and vm.wait(r"root# ", 0, 5)
print(("ok     " if ok else "ECHEC  ") + "connexion : refus puis acces")
if not ok: failures.append("login")
check(vm, "deconnexion", r"lilos login :")
vm.close()

# ---------------------------------------------------------------------------
# Reseau (carte RTL8139, reseau utilisateur de QEMU : passerelle 10.0.2.2)
# ---------------------------------------------------------------------------

vm = boot()                                          # sans carte RTL8139
check(vm, "reseau", r"aucune carte réseau")
check(vm, "ping 10.0.2.2", r"aucune carte réseau")
vm.close()

vm = boot(net=True)
check(vm, "reseau", r"carte\s+: RTL8139, MAC [0-9a-f:]{17}[\s\S]*adresse\s+: 10\.0\.2\.15")
check(vm, "arp", r"cache ARP vide")
check(vm, "ping 10.0.2.2", r"réponse de 10\.0\.2\.2 : temps=\d+ ms[\s\S]*4 envoyé\(s\), 4 reçu\(s\)", 30)
check(vm, "arp", r"10\.0\.2\.2\s+[0-9a-f]{2}(:[0-9a-f]{2}){5}")   # l'adresse MAC a ete apprise par ARP
check(vm, "ping 10.0.2.2 extra", r"usage : ping")
check(vm, "ping a.b.c", r"impossible de résoudre|nom d'hôte invalide")
check(vm, "reseau config 1.2.3.4", r"usage : reseau")
check(vm, "dns .toto", r"impossible de résoudre|nom d'hôte invalide")
check(vm, "dns " + "a" * 70 + ".com", r"nom d'hôte invalide")
if os.environ.get("LILOS_TEST_DNS"):                  # depend de la resolution du poste hote
    check(vm, "dns example.com", r"example\.com : \d+\.\d+\.\d+\.\d+", 20)
vm.close()

# les utilisateurs normaux ne reconfigurent pas le reseau
vm = boot(user="alice", pw=vmmod.USER_PW, net=True)
check(vm, "reseau config 10.0.2.20 10.0.2.2 10.0.2.3", r"permission refusée")
check(vm, "ping 10.0.2.2", r"réponse de 10\.0\.2\.2", 30)      # mais peuvent pinguer
vm.close()

if failures:
    print(f"{len(failures)} test(s) en echec")
    sys.exit(1)
print("tous les tests passent")
