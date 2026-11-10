# Sécurité

LilOS est un système d'exploitation pédagogique. Il n'a pas été audité et ne doit pas protéger de données réelles.

## Modèle de menace

- **Entrée distante** : le réseau (ARP, IPv4, ICMP, UDP, DNS). Tout paquet reçu est considéré comme hostile.
- **Entrée locale** : le clavier, et les programmes du mode utilisateur.
- **Hors périmètre** : l'accès physique à la machine, le firmware, le bootloader (pas de Secure Boot), les attaques matérielles.

## Ce qui est protégé

- **Isolation noyau et processus** : chaque processus a son espace d'adressage. Les pages du noyau ne sont jamais accessibles en ring 3. Les pointeurs reçus par les appels système sont validés. SMEP empêche le noyau d'exécuter des pages utilisateur.
- **W^X** : le code est en lecture seule, les données, la pile et le heap ne sont pas exécutables (NX, si le processeur le permet).
- **Pile** : page de garde, sondes de pile (`-fstack-clash-protection`), double fault sur pile dédiée.
- **Heap** : en-têtes avec magic, pointeurs et double libération détectés.
- **Comptes** : mots de passe salés et hachés par SHA-256 itéré, comparaison en temps constant, même travail pour un compte inconnu, temporisation après un échec, saisie sans écho.
- **Droits** : propriétaire et droits de lecture et d'écriture sur chaque fichier, appliqués aussi aux appels système. Les commandes qui exposent ou modifient le noyau sont réservées à root.
- **Réseau** : sommes de contrôle et longueurs vérifiées, fragments ignorés, pas de réponse aux requêtes en diffusion (smurf), réponses ARP acceptées seulement si elles répondent à une requête émise, réponses DNS validées (identifiant, serveur, port source aléatoire, bornes).

## Limites connues

- Pas d'ASLR, pas de canaris de pile, un seul processus à la fois, pas d'ordonnanceur.
- L'entropie (sels, identifiants DNS) vient du compteur de cycles et des ticks : suffisante pour des valeurs distinctes, pas pour de la cryptographie forte.
- L'ARP n'authentifie pas : un attaquant sur le même réseau local peut répondre à une requête légitime avant la bonne machine.
- DNS en clair, sans DNSSEC. Pas de TCP, donc pas de TLS.
- Les comptes vivent en RAM et se recréent à chaque démarrage.
- Les mots de passe transitent par le noyau en clair avant d'être hachés (mémoire effacée après usage).

## Signaler une faille

Ouvrez une issue ou écrivez en privé au propriétaire du dépôt avec les étapes pour reproduire. Merci de ne pas publier de preuve de concept avant correction.
