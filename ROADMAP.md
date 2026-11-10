# Feuille de route

Cette feuille de route est ouverte : chaque ligne devient un ticket, et chacun peut en prendre une. Dites-le dans le ticket pour qu'on ne travaille pas à deux sur la même chose.

## Fait (0.3)

- Noyau 32 bits, paging PAE, W^X, NX, SMEP, piles protégées
- Mode utilisateur (ring 3), appels système, isolation des processus
- Comptes multi-utilisateurs, mots de passe salés, droits sur les fichiers
- Réseau : RTL8139, ARP, IPv4, ICMP, UDP, DNS
- Shell en français, clavier AZERTY, accents
- Tests : fuzzers, attaques depuis le mode utilisateur, attaques réseau
- Releases signées (Ed25519) et site avec démo dans le navigateur

## Objectif : la mise à jour dans l'OS

LilOS doit pouvoir télécharger sa propre mise à jour, en vérifier la signature et redémarrer dessus, sans jamais risquer de se rendre inutilisable. Chaque brique est un ticket indépendant.

| Brique | Pourquoi |
| --- | --- |
| DHCP | obtenir une adresse sur un vrai réseau |
| TCP | indispensable pour HTTP |
| Client HTTP | télécharger le manifeste et l'image |
| Vérification Ed25519 | n'accepter que ce que le projet a signé (la clé publique est dans `release/`) |
| Pilote disque ATA (PIO) | écrire la nouvelle image sur le disque |
| Deux emplacements (A/B) et retour arrière | une mise à jour interrompue ne doit pas bloquer le démarrage |
| Protection contre le retour à une ancienne version | une version signée mais vulnérable ne doit pas être réinstallable |
| Commande `miseajour` | l'interface pour l'utilisateur |

Le TLS viendra plus tard. Tant qu'il manque, la sécurité de la mise à jour repose sur la signature (une image modifiée est refusée), pas sur le transport.

## Ensuite

- Ordonnanceur et plusieurs processus en même temps
- Système de fichiers persistant sur disque
- Pilote clavier USB et pilote réseau supplémentaire (e1000)
- Mémoire virtuelle par processus avec tas utilisateur (`brk`)
- Une vraie bibliothèque C pour les programmes utilisateur
- Support UEFI
- Documentation pas à pas pour apprendre en lisant le noyau

## Comment proposer

Ouvrez une [discussion](https://github.com/abiyeenzo/lilos/discussions) pour une idée, ou un ticket pour une tâche précise.
