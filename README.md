# LilOS

Système d'exploitation x86 32 bits écrit from scratch, en français : bootloader NASM, noyau en C, mode utilisateur, comptes multi-utilisateurs, pile réseau et shell. Il démarre en BIOS (QEMU ou machine réelle en mode legacy).

## Fonctions

- **Démarrage** : secteur de boot BIOS, lecture du noyau par LBA, A20, mode protégé.
- **Noyau** : GDT, IDT, PIC, timer 100 Hz, clavier AZERTY (AltGr compris), VGA avec accents (UTF-8 vers CP437), port série, heap vérifié, système de fichiers en RAM avec propriétaires et droits.
- **Mémoire** : paging PAE, W^X (code en lecture seule, données non exécutables via NX), SMEP, page de garde de pile, double fault sur pile dédiée, `-fstack-clash-protection`.
- **Mode utilisateur** : processus en ring 3 avec leur propre espace d'adressage, appels système par `int 0x80`, pointeurs validés, Ctrl+C. Un processus qui touche au noyau est tué sans arrêter le système.
- **Multi-utilisateur** : comptes `root` et utilisateurs, mots de passe salés (SHA-256 itéré), connexion avec temporisation après un échec, `su`, droits sur les fichiers, commandes d'administration réservées à root.
- **Réseau** : pilote RTL8139, ARP, IPv4, ICMP (ping), UDP, client DNS. Pas de TCP ni d'IPv6.
- **Shell** : commandes en français (les noms anglais restent acceptés), historique, saisie accentuée.

## Compiler et lancer

Dépendances : `nasm`, `gcc` avec support 32 bits (`gcc-multilib`), `binutils`, `qemu-system-i386`, `python3`.

```
make              # produit build/lilos.img
make run          # lance dans QEMU (réseau RTL8139 inclus)
make run-serial   # sans fenêtre, sortie sur le terminal
```

Au premier démarrage, LilOS demande le mot de passe de `root` puis crée un compte utilisateur. Les comptes et fichiers vivent en RAM : tout est recréé à chaque démarrage. Il faut au moins 32 Mo de RAM.

Pour le réseau, QEMU doit émuler une RTL8139 : `qemu-system-i386 -drive format=raw,file=build/lilos.img -nic user,model=rtl8139`. Les adresses par défaut sont celles du réseau utilisateur de QEMU (10.0.2.15, passerelle 10.0.2.2, DNS 10.0.2.3).

## Premiers pas

```
aide                          # liste des commandes
ecrire notes.txt café à côté  # crée un fichier (accents compris)
afficher notes.txt
droits notes.txt 60           # lecture et écriture pour le propriétaire seulement
programmes                    # programmes utilisateur embarqués
lancer bonjour                # exécution en ring 3
lancer noyau                  # tente de lire le noyau : le processus est tué
ping 10.0.2.2                 # passerelle
dns example.com
```

Commandes réservées à root : `hexdump`, `memoire`, `creeruser`, `supprimeruser`, `proprietaire`, `redemarrer`, `eteindre`, `proteger`, `panic`, `nulltest`, `stacktest`.

## Organisation

| Chemin | Rôle |
| --- | --- |
| `bootloader/` | secteur de boot |
| `kernel/` | noyau : interruptions, paging, heap, fichiers, comptes, processus, réseau, shell |
| `user/` | programmes du mode utilisateur et leur bibliothèque |
| `tests/` | tests hôte, fuzzers et tests QEMU |

Mémoire physique : `0x7C00` bootloader, `0x10000` noyau, `0x90000` sommet de la pile, `0x100000` à `0x1000000` heap, `0x1000000` à `0x2000000` pages des processus. Les processus voient `0x40000000` à `0x40100000`.

## Tests

```
make test-heap test-lib test-fs test-sha test-net   # tests hôte avec ASan et UBSan
make test                                           # QEMU : clavier, comptes, ring 3, réseau
make test-attack                                    # 51 attaques depuis le mode utilisateur
make test-netattack                                  # paquets hostiles envoyés à la VM
make fuzz-shell SEED=1 ROUNDS=100                   # touches aléatoires dans le shell
```

Les tests QEMU tapent de vraies touches AZERTY via le moniteur et lisent la sortie série. Le fuzzer réseau envoie des trames valides, mutées et aléatoires à la pile. La CI lance tout à chaque push.

## Sécurité

Voir [SECURITY.md](SECURITY.md) pour le modèle de menace, ce qui est protégé et ce qui ne l'est pas.

## Licence

MIT
