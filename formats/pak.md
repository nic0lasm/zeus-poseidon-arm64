# `.pak` — aventure publiée

Une aventure jouable : l'en-tête d'un `.set` ([`set.md`](set.md)) suivi des cartes de
l'aventure, conteneurs MAPS ([`map.md`](map.md)) recopiés bout à bout. Adresses de
l'exécutable GOG ; tout est little-endian.

Outil : `tools/pakfile.py` (`info`, `check`). État au 2026-10-15 : les 130 `.pak` des
installations (56 distincts, versions 5 et 26, 259 cartes) se lisent jusqu'au dernier octet et
se réécrivent à l'identique.

## Structure

| Partie | Taille | Contenu | Statut |
|---|---|---|---|
| en-tête | 842 931 octets (v26), 841 863 (v5) | exactement un `.set` : 54 champs de l'objet aventure puis 4 variables globales | vérifié |
| cartes | jusqu'à la fin du fichier | 1 à 5 conteneurs MAPS : ville mère, puis colonies 1 à 4, chacun copie octet pour octet d'un fichier `.map` | vérifié |

## Écriture (`0x0042AD86`, publication depuis l'éditeur)

1. `0x00425910` écrit l'en-tête, `0x0052DEF0` la suite (variables globales) ; `ftell` donne le
   décalage de la première carte.
2. `0x0042A450` parcourt les codes 0, 10, 11, 12, 13 (ville mère, colonies 1 à 4). Pour chaque
   fichier de carte qui existe (nom donné par `0x004267A0`), il le lit en entier, au plus
   500 000 octets, **dans le champ `map_buffer` de l'objet aventure** (+0x4954C), note le
   décalage courant dans `map_offsets`, recopie les octets lus et avance.
3. Retour au début du fichier et nouvelle écriture de l'en-tête par `0x00425910`, cette fois
   avec les bons décalages.

Conséquences, toutes vérifiées sur les 56 fichiers distincts :

- `map_offsets` donne le début de chaque carte présente ; l'entrée d'une carte absente garde
  une valeur ancienne, parfois au milieu d'une autre carte ou au-delà de la fin du fichier ;
- `map_buffer` commence par la dernière carte recopiée ; ce qui suit est le reste de cartes
  plus longues lues auparavant (zéros dans 4 fichiers seulement) ;
- chaque carte est le fichier `.map` d'origine : sa compression a été faite par l'éditeur, avec
  une structure de travail neuve par fichier.

## Lecture

`0x0042B230` ouvre `Adventures\NOM.pak`, ou `Adventures\NOM\NOM.pak` quand le genre (`kind`) vaut
1 ou 4, lit l'en-tête (`0x00425D00`, `0x0052DF50`), choisit la carte selon `0x00F2A7B0` (0 : ville
mère ; 10 à 13 : colonies), se place à son décalage et la lit avec `0x0050DF50`. Les noms des
16 didacticiels figurent en clair dans l'exécutable (`housing.pak` à `bigcity.pak`, à partir de
`0x0065C508`).

## Fichiers testés

- `original/*/Adventures/**/*.pak` : 130 fichiers, 56 distincts.
- Les 4 fichiers `original/patch-zeus-1.1/0xx__*.pak` n'ont pas la signature `0x22A7` : ce sont
  des morceaux compressés de l'installeur du correctif 1.1, pas des aventures ; `check` les
  signale et les ignore.
