# `.map` et `.sav` — cartes et sauvegardes

Lecteurs : `tools/mapfile.py` (cartes et sauvegardes), `src/formats/mapfile.cpp` (cartes).
Tout est little-endian. Les sauvegardes ont leur propre fiche ([`sav.md`](sav.md)).

`python3 tools/mapfile.py rewrite original` relit les 57 cartes des installations (20 distinctes,
versions 247, 318, 319 et 321) et les réécrit à l'octet près, compression comprise (vérifié le
2026-10-15).

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
| 16 | u32 | adresse d'une liste de convertisseurs de version, dans l'exécutable qui a écrit le fichier ; lue seulement dans la table statique de l'exécutable qui lit (`0x0050CB80`) | vérifié |

Ordre des blocs au début du fichier :

| Carte `.map` | Sauvegarde `.sav` | Contenu |
|---|---|---|
| bloc 0 | — | signature `MAPS` |
| bloc 1 | bloc 0 | version (247, 318, 321 pour les cartes ; 235 à 241 pour les sauvegardes livrées) |
| bloc 2 | bloc 1 | nombre de blocs (33 pour une carte, 138 pour une sauvegarde) |
| bloc 3 | bloc 2 | la table : 300 entrées de 20 octets |

Un bloc brut occupe `taille × nombre` octets. Un bloc compressé commence par un `u32` : la
longueur du flux qui suit, ou `0x80000000` si le bloc est finalement stocké brut (0 : bloc rempli
de zéros à la lecture). Le flux est du **PKWARE DCL « implode »** en mode binaire (en-tête
`00 06`), décodé par `explode()` ; la compression et le détail qui permet de retrouver les
octets du jeu sont décrits dans [`sav.md`](sav.md#compression).

## Routines (exécutable GOG)

| Adresse | Rôle | Statut |
|---|---|---|
| `0x005118C0` | écriture d'une carte depuis l'éditeur : version 321, 33 blocs de la table `0x0063CDC0`, mêmes écritures brutes ou compressées que les sauvegardes | vérifié |
| `0x0050DF50` | lecture : bloc 0 (`MAPS`) comparé à la chaîne `0x0063CDB4`, puis blocs 1 à 32 par `0x0050CB80` avec la table `0x0063CDC0`, ou `0x0063D058` / `0x0063D2F0` selon l'octet `0x00F2A6D9` (`&` ou `(`) | vérifié |
| `0x00512510` | au démarrage, remplit les emplacements réservés des quatre tables (`0x0050D130`) | vérifié |

Dans l'exécutable GOG, les blocs 0 à 17 de la table `0x0063CDC0` sont : `MAPS` (`0x0063CDAC`),
version, nombre de blocs, table, puis `0x00D4CFC8` (u32), `0x00D404B8`, `0x00CCE128` (u32),
`0x00D00D68`, `0x00CA7FF8`, `0x00C8E9D8`, quatre entiers (`0x00F137E4`, `0x00F137E0`,
`0x00DA2F64`, `0x00DA2F68`), `0x00902928` (2 032 octets), `0x00C35C68`, `0x009173F0`
(18 628 octets) et `0x008FEBE8` (200 × 72). Ce sont les variables des rangs 15, 17, 20, 22, 24,
42 à 45, 9, 99, 13–14 et 10 des sauvegardes, plus la grille d'images `0x00D4CFC8`, absente des
sauvegardes. Les entrées 18 à 32 sont nulles dans l'exécutable sur disque et remplies à l'exécution (endroit non localisé).

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

Les blocs d'une sauvegarde sont décrits dans [`sav.md`](sav.md). Une sauvegarde **ne contient pas de grille d'images** : le jeu la recalcule au chargement à
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
**Vérifié** dans `0x00476240` : pour un élément de taille n, le jeu décale le point de dessin de
`+30(n−1), −15(n−1)` depuis la case de dessin, ce qui revient au même. La taille vient des
4 bits bas de la grille 7 (taille − 1), pas de la largeur de l'image.

## Relief

- Bloc 23 des `.map` : altitude de chaque case, de 0 à 6 (**vérifié** : la fonction `0x0040E5D0`
  lit l'octet `objet + 2 + case`, et l'objet `0x0097C448` de la 2.1 correspond à l'adresse du
  bloc 23 moins 2). Le bloc 24 a la même forme ; son rôle n'est pas établi.
- La boucle de dessin `0x0047499F` dessine une case à `y − altitude × 30` (**vérifié** dans le
  code, et visible au rendu : les plateaux sont surélevés).
- Drapeau de terrain `0x200` : case de falaise. Elle porte l'altitude du niveau **bas**.
- Le jeu dessine les socles puis les parties hautes, et ne dessine jamais le socle de 40 groupes
  de falaises (table `0x00692F38`, test `0x00476B90`) : ce sont les falaises orientées N, O et
  NO, dont le socle est entièrement en vert `0x03E0`. Ce vert n'est donc pas une couleur-clé du
  moteur ; ne pas le dessiner revient au même pour ces images.
- Les falaises orientées E, S, SE et SO ont un vrai socle.

## Drapeaux de terrain (bloc 6)

Disposition héritée des jeux précédents du studio, recoupée avec le code et les comptages :

| Bit | Sens | Preuve |
|---|---|---|
| `0x1` | arbre | 3 521 cases sur la carte d'Atlantide pour 3 499 images d'arbres |
| `0x2` | fissures (avec le bit `0x80` de la grille 7) | `0x004D3D28` pose le bit et choisit dans `SPR_CRACKS` |
| `0x4` | eau | 7 144 cases pour 7 305 images d'eau ; animation dans `0x004743EA` |
| `0x8` | bâtiment | tests de la boucle de dessin |
| `0x200` | falaise | voir Relief |
| `0x4000000` | eau profonde : animation sur les 90 images de `SPR_WATER_ANIMS` (6 étapes de 15) | `0x004743EA` |

Les autres bits (`0x80`, `0x800`, `0x80000`, `0x400000`…) ne sont pas établis.

## Choix des images de terrain

Les fonctions `0x004D3D28` à `0x004D9DA0` recalculent l'image d'une case d'après ses 8 voisins
(N, NE, E, SE, S, SO, O, NO : décalages −228, −227, +1, +229, +228, +227, −1, −229). La
recherche `0x004E3616` parcourt une table d'enregistrements de 20 octets : 8 octets de motif
(0 = voisin absent exigé, 1 = présent exigé, 2 = indifférent), 4 décalages de groupe (un par
orientation de la vue), puis nombre de variantes et compteur ; la variante est tirée de la
grille aléatoire. Tables repérées : `0x006D1220` (17 entrées, fissures), `0x006CF600`,
`0x006CFD80`, `0x006CF240`, `0x006D1378`, `0x006D1BF0`, `0x006D1E70`, `0x006D0E60`,
`0x006D16D8`, `0x006D1830`. Non porté : les `.map` contiennent déjà les images calculées.

## Écarts connus avec le jeu

Comparaison avec des captures du jeu (page GOG, galerie de masterofolympus.com) :

- **Rivages** : conformes. Les tuiles de `SPR_WATER` contiennent le rivage rocheux arrondi ; un
  rivage horizontal à l'écran forme un escalier, comme dans le jeu. (Une première lecture avait
  conclu à tort que le jeu recalculait ces images.)
- **Ordre de dessin** : porté depuis `0x0047499F` dans `src/game/terrain_view.cpp` (première
  passe de socles, puis socles différés et parties hautes du fond vers l'avant, avec les tests
  `0x0047A600`, `0x0047A6C0`, `0x0047A7C0`, `0x00476B90`). Les branches liées aux bâtiments et
  aux personnages ne sont pas portées.
- **Trous au pied de certaines falaises** (impasse au 2026-10-13) : il reste des losanges noirs
  à l'emplacement du socle de falaises « de fond » (`E2_NW_ALT`, `E2_N`, `E2_W_N`…) quand
  aucune case surélevée ne vient s'y projeter, typiquement le long d'un bord de plateau
  vertical à l'écran. Constats : le jeu ne dessine jamais ce socle ; rien d'autre dans la boucle
  de dessin ne peint cet emplacement ; la deuxième grille d'altitude (bloc 24, recalculée par
  `0x0047CB00` et `0x0047FDB0`) vaut 0 à ces endroits, donc le jeu lui-même considère qu'aucune
  case surélevée ne s'y projette. Or les captures du jeu ne montrent aucun trou. Pistes non
  explorées : le positionnement exact dans `0x0064274C` et `0x0064394C` (dessin du socle et de
  la partie haute), et les règles d'attribution des images de falaise (`0x0047CCE4`,
  `0x0047BE8D`).
- **Bloc 24** : pour chaque case d'altitude e, la case située e rangs derrière reçoit e : c'est
  l'altitude de ce qui se projette à cet emplacement de l'écran (dépend de l'orientation).
- **Cases `SPR_SPACE`** (noires) : `0x0047CCE4` attribue cette image aux cases sans falaise
  dont un voisin est plus haut, c'est-à-dire à des cases censées être cachées. Sur les cartes
  plates, elles portent les repères de l'éditeur. Elles restent visibles dans notre rendu.
- **Repères d'éditeur** (points d'apparition) : affichés tels quels.
- **Eau** : fixe ; l'animation est décrite ci-dessus mais pas portée.
- **Sauvegardes** : elles n'ont pas de grille d'images ; les afficher demande de porter le
  choix des images de terrain.
