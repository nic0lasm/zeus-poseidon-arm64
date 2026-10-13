# `.map` et `.sav` — cartes et sauvegardes

Lecteurs : `tools/mapfile.py` (cartes et sauvegardes), `src/formats/mapfile.cpp` (cartes).
Tout est little-endian.

État au 2026-10-13, sur `original/install` : **16 `.map` et 16 `.sav` lus en entier**, jusqu'au
dernier octet. Empreinte des blocs décompressés identique entre Python et C++ sur les 16 cartes.
Les `.pak` (40) et `.set` (8) ont une autre structure, non décodée.

## Conteneur

Le fichier est une suite de **blocs**, chacun étant l'image mémoire d'une variable du jeu. Une
table, elle-même enregistrée comme un bloc, décrit chaque bloc en 20 octets :

| Offset | Type | Champ | Statut |
|---|---|---|---|
| 0 | u32 | 1 = bloc compressé, 0 = brut | vérifié |
| 4 | u32 | adresse de la variable dans l'exécutable qui a écrit le fichier | vérifié |
| 8 | u32 | taille d'un élément | vérifié |
| 12 | u32 | nombre d'éléments | vérifié |
| 16 | u32 | ? (non nul pour 2 blocs, ressemble à une adresse) | inconnu |

Ordre des blocs au début du fichier :

| Carte `.map` | Sauvegarde `.sav` | Contenu |
|---|---|---|
| bloc 0 | — | signature `MAPS` |
| bloc 1 | bloc 0 | version (247, 318, 321 pour les cartes ; 235 à 241 pour les sauvegardes livrées) |
| bloc 2 | bloc 1 | nombre de blocs (33 pour une carte, 138 pour une sauvegarde) |
| bloc 3 | bloc 2 | la table : 300 entrées de 20 octets |

Un bloc brut occupe `taille × nombre` octets. Un bloc compressé commence par un `u32` : la
longueur du flux qui suit, ou `0x80000000` si le bloc est finalement stocké brut. Le flux est du
**PKWARE DCL « implode »** en mode binaire (en-tête `00 06`), décodé par `explode()`.

Les adresses changent selon la version de l'exécutable qui a enregistré le fichier (8 jeux
d'adresses pour 16 cartes), mais **l'ordre et la forme des blocs sont identiques dans toutes
les cartes**. Un bloc s'identifie donc par son rang. Les adresses des cartes en version 321
sont celles de `Zeus.exe` 2.1 : elles relient directement chaque bloc à une variable du code.

## Grilles

La carte est une grille de 228 × 228 cases (51 984). Seule la moitié des cases est utilisée :
la zone jouable est un rectangle à l'écran, donc un losange dans la grille.

Blocs d'une carte `.map` (rang, adresse dans la 2.1) :

| Rang | Adresse | Type | Contenu | Statut |
|---|---|---|---|---|
| 4 | `0x01216248` | u32 | identifiant d'image de la case | vérifié (rendu) |
| 5 | `0x01209738` | u8 | bit `0x40` : case où l'image est dessinée ; bits bas : position dans un élément de plusieurs cases | hypothèse cohérente avec le rendu |
| 6 | `0x011973A8` | u32 | drapeaux de terrain (42 combinaisons sur la carte d'Atlantide) | hypothèse |
| 7 | `0x011C9FE8` | u8 | bits `0x20`, `0x01`, `0x02` | inconnu |
| 8 | `0x01171278` | u8 | valeurs aléatoires (256 valeurs, incompressible) | hypothèse |
| 9, 15, 19, 22 à 24 | | u8 | inconnu | |

Une sauvegarde **ne contient pas de grille d'images** : le jeu la recalcule au chargement à
partir du terrain et des bâtiments. On y trouve notamment (rangs dans une sauvegarde 2xx) :
15 (u8, bits `0x40`…), 16 (u16, max 629 : numéros de bâtiment ?), 17 (u32, drapeaux de terrain),
19 (u16 : numéros de personnage ?), 22 (u8 aléatoire).

## Identifiant d'image

`identifiant = collection × 0x4000 + numéro` — **vérifié** par le code (fonctions `0x0040DF00`,
`0x00641DF4`, `0x0065DAFB`) et par le rendu.

- `numéro` compte à partir de 1 **sans** les 200 images « système » (`Zeus_system.bmp`) placées
  en tête de chaque fichier : image du fichier = numéro + 200. La collection 0 fait exception.
- Collections fixes : 0 `Zeus_Fonts`, 1 `Zeus_Interface`, 2 `Zeus_Unloaded`,
  3 `Poseidon_Unloaded`, 4 `Zeus_General`, 5 `Zeus_Terrain`, 6 `Zeus_Sanctuary3`,
  7 `Zeus_Sanctuary1`, 8 à 13 `Zeus_GOD01`…`06`, 14 `Zeus_STATCON1`, 15 `Zeus_MapOfGreece01`,
  16 `SprAmbient`, 17 `Destruction`, 51 `SprMain`, 52 `Poseidon_Loaded`, 53 `PoseidonImps`,
  65 `Poseidon_GrandStatue1`, 66 `PoseidonImps2`. Les autres sont attribuées en cours de partie.
- Cartes antérieures à Poséidon (version 247) : le terrain y est la collection 3. Le rendu est
  correct en ajoutant 2 aux collections ≥ 2 ; la conversion faite par le jeu reste à retrouver
  dans le code (**hypothèse**).

## Projection utilisée par `zeus-map`

Coin haut-gauche du losange de la case (x, y) : `((x − y) × 30, (x + y) × 15)`. Les cases sont
dessinées par diagonales croissantes `x + y`. Une image de `n` cases de côté (largeur 60n − 2)
est posée avec son socle de 30n lignes calé en bas, la case de dessin étant son coin gauche.
C'est la convention des jeux précédents du studio : **hypothèse**, cohérente avec le rendu des
cartes mais pas encore lue dans la routine de dessin.

## Écarts connus avec le jeu

Les `.map` sont des cartes de l'éditeur. En les comparant à des captures officielles du jeu :

- **Rivages** : le jeu affiche des plages arrondies ; la grille d'images des `.map` donne des
  bords en dents de scie. Le jeu recalcule donc les images de terrain à partir des drapeaux
  (bloc 6) au chargement. Ces règles sont à retrouver dans le code.
- **Cases noires** (`SPR_SPACE`) et repères d'éditeur (points d'apparition) : remplacés en jeu.
- **Relief** : le socle des tuiles de falaise est en vert pur (`0x03E0`), traité comme
  transparent ; ce que le jeu dessine dessous n'est pas établi (trous noirs dans notre rendu).
