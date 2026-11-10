# Sécurité

LilOS est un système d'exploitation pédagogique. Il n'a pas été audité et ne doit pas protéger de données réelles.

## Modèle de menace

- **Entrée distante** : le réseau (ARP, IPv4, ICMP, UDP, DNS). Tout paquet reçu est considéré comme hostile.
- **Entrée locale** : le clavier, et les programmes du mode utilisateur.
- **Hors périmètre** : l'accès physique à la machine, le firmware, le bootloader (pas de Secure Boot), les attaques matérielles.

## Ce qui est protégé

- **Ports d'E/S** : aucun port matériel n'est accessible en ring 3 (pas de table d'autorisations d'E/S, IOPL à 0). Un processus ne peut ni espionner le clavier, ni reprogrammer le PIC ou le bus PCI.
- **Isolation noyau et processus** : chaque processus a son espace d'adressage. Les pages du noyau ne sont jamais accessibles en ring 3. Les pointeurs reçus par les appels système sont validés. SMEP empêche le noyau d'exécuter des pages utilisateur.
- **W^X** : le code est en lecture seule, les données, la pile et le heap ne sont pas exécutables (NX, si le processeur le permet).
- **Pile** : page de garde, sondes de pile (`-fstack-clash-protection`), double fault sur pile dédiée.
- **Heap** : en-têtes avec magic, pointeurs et double libération détectés.
- **Comptes** : mots de passe salés et hachés par SHA-256 itéré, comparaison en temps constant, même travail pour un compte inconnu, temporisation après un échec, saisie sans écho.
- **Droits** : propriétaire et droits de lecture et d'écriture sur chaque fichier, appliqués aussi aux appels système. Les commandes qui exposent ou modifient le noyau sont réservées à root.
- **Pilote réseau** : le DMA de réception ne peut pas sortir de son tampon (anneau sans WRAP, les trames coupées en fin d'anneau sont recollées par le pilote). Les trames invalides ou surdimensionnées sont sautées, sans réinitialiser la carte.
- **Réseau** : sommes de contrôle et longueurs vérifiées, fragments ignorés, pas de réponse aux requêtes en diffusion (smurf), réponses ARP acceptées seulement si elles répondent à une requête émise, réponses DNS validées (identifiant, serveur, port source aléatoire, bornes).

## Tests d'attaque

- `make test-attack` : 51 attaques lancées depuis un processus utilisateur non privilégié (appels système avec pointeurs hostiles, instructions privilégiées, accès aux ports, drapeaux CPU, débordements). Chaque attaque doit être refusée ou tuer le processus, et le système doit continuer à fonctionner.
- `make test-netattack` : un script joue le rôle du réseau et envoie à la VM des paquets hostiles (ARP empoisonné, ping en diffusion, fragments, sommes fausses, inondation, 6000 trames mutées, trames surdimensionnées, fausses réponses DNS).
- Failles trouvées et corrigées par ces tests : ports d'E/S accessibles depuis le ring 3 (espionnage du clavier, arrêt du système), débordement DMA possible du tampon de réception, réinitialisation incohérente du pilote.

## Limites connues

- Pas d'ASLR, pas de canaris de pile, un seul processus à la fois, pas d'ordonnanceur.
- L'entropie (sels, identifiants DNS) vient du compteur de cycles et des ticks : suffisante pour des valeurs distinctes, pas pour de la cryptographie forte.
- L'ARP n'authentifie pas : un attaquant sur le même réseau local peut répondre à une requête légitime avant la bonne machine.
- DNS en clair, sans DNSSEC. Pas de TCP, donc pas de TLS.
- Les comptes vivent en RAM et se recréent à chaque démarrage.
- Les mots de passe transitent par le noyau en clair avant d'être hachés (mémoire effacée après usage).

## Signaler une faille

Ouvrez une issue ou écrivez en privé au propriétaire du dépôt avec les étapes pour reproduire. Merci de ne pas publier de preuve de concept avant correction.
