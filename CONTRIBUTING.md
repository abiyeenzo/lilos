# Contribuer à LilOS

Merci de votre intérêt. LilOS est un système d'exploitation écrit de zéro, ouvert à tous : on y apprend, on y casse des choses, on y corrige des failles. Que vous ayez écrit un noyau ou non, il y a une place pour vous.

## Par où commencer

- Les tickets marqués [`good first issue`](https://github.com/abiyeenzo/lilos/labels/good%20first%20issue) sont faits pour un premier pas : petits, bien décrits, avec une piste.
- Les tickets [`help wanted`](https://github.com/abiyeenzo/lilos/labels/help%20wanted) sont de vrais chantiers où une aide serait la bienvenue.
- La [feuille de route](ROADMAP.md) montre où va le projet.
- Une idée, une question ou une démo à montrer ? Les [Discussions](https://github.com/abiyeenzo/lilos/discussions) sont faites pour ça.
- Vous avez trouvé une faille de sécurité ? Ne publiez rien : suivez [SECURITY.md](SECURITY.md).

## Mettre en place l'environnement

Dépendances : `nasm`, `gcc` avec support 32 bits (`gcc-multilib`), `binutils`, `qemu-system-x86`, `python3`, `openssl`.

```
git clone https://github.com/abiyeenzo/lilos && cd lilos
make            # construit build/lilos.img
make run        # lance dans QEMU
```

Au premier démarrage, LilOS demande un mot de passe root puis crée un compte. Tapez `aide` pour la liste des commandes.

## Lancer les tests

| Commande | Ce qu'elle vérifie |
| --- | --- |
| `make test-heap test-lib test-fs test-sha test-net` | allocateur, fonctions de base, fichiers, SHA-256, pile réseau (hôte, avec ASan et UBSan) |
| `make test` | clavier AZERTY, comptes, mode utilisateur, réseau, dans QEMU |
| `make test-attack` | 51 attaques depuis un processus non privilégié |
| `make test-netattack` | paquets hostiles envoyés à la machine virtuelle |
| `make fuzz-shell SEED=1 ROUNDS=100` | touches aléatoires dans le shell |

La CI lance tout à chaque push et à chaque pull request. Une PR dont la CI est rouge ne sera pas fusionnée.

## Proposer un changement

1. Forkez le dépôt et créez une branche à partir de `main` (`correction-heap`, `commande-uptime`...).
2. Gardez la PR petite et centrée sur un seul sujet. Deux sujets, deux PR.
3. Ajoutez ou adaptez un test. Un bug corrigé sans test de non-régression reviendra.
4. Lancez les tests concernés en local avant de pousser.
5. Ouvrez la PR en remplissant le modèle. Décrivez le pourquoi, pas seulement le quoi.

Messages de commit : courts, factuels, à l'impératif ou au présent (`Corrige le débordement de kmalloc`). Une ligne suffit le plus souvent.

## Style du code

- C11 freestanding : pas de libc, pas d'allocation dynamique hors `kmalloc`, pas de tableaux de taille variable.
- Chaque donnée venue de l'extérieur (réseau, utilisateur, processus) est vérifiée avant usage : longueurs, bornes, débordements d'entiers.
- Commentaires rares et utiles : expliquez le pourquoi qui n'est pas évident. Pas de paraphrase du code.
- Messages affichés à l'utilisateur en français, avec les accents.
- Pas de dépendance externe dans le noyau.

## Zones sensibles

Ces zones touchent à la sécurité : elles demandent un test d'attaque ou un fuzzer avec le changement, et seront relues avec soin.

- `kernel/user.c`, `kernel/interrupts.c`, `kernel/paging.c` : frontière entre le noyau et les processus
- `kernel/net.c`, `kernel/rtl8139.c` : tout ce qui vient du réseau est hostile
- `kernel/users.c`, `kernel/sha256.c`, `kernel/fs.c` : comptes, mots de passe, droits

## Ajouter une commande au shell

1. Écrivez la fonction `cmd_...` dans `kernel/shell.c`.
2. Ajoutez une ligne à la table `commands[]` : nom français, alias anglais éventuel, aide, fonction, et `1` si elle est réservée à root.
3. Ajoutez un test dans `tests/qemu_test.py`.

## Ajouter un programme du mode utilisateur

1. Créez `user/mon_programme.c` avec `int main(int argc, char **argv)`, en vous servant de `user/ulib.h`.
2. Ajoutez son nom à `USER_PROGS` dans le `Makefile` et à `kernel/programs.c`.
3. Lancez-le avec `lancer mon_programme`.

## Ajouter un appel système

Ajoutez un `case` dans `syscall_dispatch` (`kernel/user.c`). Validez chaque pointeur avec `uptr_ok`, refusez les longueurs excessives, et ajoutez un cas dans `user/attaque.c` et `tests/attack_test.py` qui tente de le détourner.

## Licence

En contribuant, vous acceptez que votre code soit publié sous la licence MIT du projet.

## Vos questions

Si quelque chose n'est pas clair, ouvrez une discussion. Les questions de débutant sont les bienvenues : elles montrent ce que la documentation doit expliquer.
