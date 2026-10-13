# `.sg3` + `.555` — sprites

Un fichier `.sg3` est un index ; les pixels sont dans le `.555` de même nom (couleurs 15 bits,
d'où l'extension). Lecteur : `tools/sg3.py`. Tout est little-endian.

État au 2026-10-13, sur `original/install/DATA` : **90 fichiers `.sg3`, 70 587 images**.
70 395 se décodent sans erreur ; les 192 restantes sont celles de `Zeus_MapOfGreece01.sg3`, dont
le `.555` n'existe sur aucun support (le jeu livre la carte en JPG). Les données internes sont
parfaitement contiguës (0 rupture sur l'ensemble), ce qui valide offsets et longueurs.

## Structure du `.sg3`

| Offset | Taille | Contenu |
|---|---|---|
| 0 | 680 | en-tête |
| 680 | 200 × 200 | enregistrements de bitmap |
| 40 680 | max × R | enregistrements d'image (R = 64 en version 0xD5, 72 en 0xD6) |
| fin − 14 400 | 300 × 48 | noms des groupes, chaînes C (`SPR_HERO_STATUES`…) |

Taille totale : 695 080 octets (0xD5, max = 10 000) ou 991 080 (0xD6, max = 13 000). Vérifié sur
les 90 fichiers : 35 en 0xD5 (Zeus), 55 en 0xD6 (Poséidon).

### En-tête

| Offset | Type | Champ | Statut |
|---|---|---|---|
| 0 | u32 | taille d'en-tête ou du fichier (74 480 en 0xD5, taille du fichier en 0xD6) | inconnu |
| 4 | u32 | version : 0xD5 ou 0xD6 | vérifié |
| 8 | u32 | ? | inconnu |
| 12 | u32 | nombre maximal d'images | vérifié |
| 16 | u32 | nombre d'images (l'image 0 est un enregistrement vide en plus) | vérifié |
| 20 | u32 | nombre de bitmaps | vérifié |
| 24 | u32 | nombre de bitmaps − 1 | hypothèse |
| 28 | u32 | taille totale des pixels (interne + externe) | hypothèse |
| 32 | u32 | taille du `.555` ; le fichier réel fait 4 octets de plus en 0xD5 | vérifié |
| 36 | u32 | taille des pixels externes | hypothèse |
| 80 | u16 × 300 | première image de chaque groupe, dans l'ordre des noms de groupes | vérifié |

### Enregistrement de bitmap (200 octets)

| Offset | Type | Champ |
|---|---|---|
| 0 | char[65] | nom du fichier source (`Zeus_Culture.BMP`) |
| 65 | char[51] | commentaire |
| 116 | u32 × 5 | largeur, hauteur, nombre d'images, première image, dernière image |
| 136 | 64 | inconnu |

### Enregistrement d'image

| Offset | Type | Champ | Statut |
|---|---|---|---|
| 0 | u32 | offset des données dans le `.555` | vérifié |
| 4 | u32 | longueur des données | vérifié |
| 8 | u32 | longueur de la partie non compressée (socle isométrique) | vérifié |
| 16 | i32 | renvoi miroir : si non nul, l'image est celle d'indice `i + renvoi`, retournée horizontalement (18 546 images) | vérifié |
| 20 | i16 × 2 | largeur, hauteur | vérifié |
| 30 | u16 | nombre d'images de l'animation | hypothèse |
| 34 | i16 × 2 | point d'ancrage x, y du sprite | hypothèse |
| 50 | u8 | type : 0, 1, 10, 12, 13, 20, 30 | vérifié |
| 51 | u8 | 1 = entièrement compressée | vérifié |
| 52 | u8 | 1 = pixels dans un `.555` externe, nommé d'après le bitmap ; l'offset est alors décalé de 1 | vérifié (313 images) |
| 53 | u8 | 1 = partie compressée après le socle | hypothèse |
| 55 | u8 | taille du socle isométrique en cases (1 à 6) | vérifié |
| 56 | u8 | indice du bitmap | vérifié |
| 62 | u8 | 1 = image miroir (coïncide avec le renvoi non nul) | vérifié |
| 64 | u32 | (0xD6) ? — vaut `offset + 2 × longueur` pour 44 % des images | inconnu |
| 68 | u32 | (0xD6) longueur du masque alpha, stocké juste après les pixels | vérifié |

Octets non nuls encore inexpliqués : 24, 26, 28, 32, 48, 57 à 61, 63.

## Codage des pixels

Couleur : `u16`, 5 bits par composante, `0RRRRRGGGGGBBBBB`. Le bit 15 est à 1 sur une grande partie des pixels
sans effet visible sur la couleur : son rôle n'est pas établi.

| Cas | Images | Codage |
|---|---|---|
| non compressé (types 0, 1, 10, 12, 13) | 3 000 | largeur × hauteur `u16` ; `0xF81F` = transparent |
| compressé (octet 51 = 1) | 65 200 | suites : `255, n` = n pixels transparents ; sinon `c` = c pixels `u16` qui suivent. Les suites franchissent les fins de ligne. |
| isométrique (type 30) | 2 387 | socle non compressé, puis éventuelle partie haute en suites  Dans le socle, le vert pur `0x03E0` (bit 15 ignoré) est une couleur-clé non dessinée : 84 tuiles de relief ont un socle entièrement vert. |
| masque alpha (0xD6) | — | mêmes suites, un octet par pixel, opacité de 0 à 31 |

Socle isométrique : largeur = 60 × taille − 2, hauteur du socle = 30 × taille, calé en bas de
l'image. Il est stocké case par case (losange de 58 × 30, 900 pixels, 1 800 octets), par rangées
du haut vers le bas : la rangée r contient r + 1 cases puis décroît, chaque case décalée de
60 pixels en x et chaque rangée de 15 en y. Dans un losange, la ligne y couvre les colonnes
`28 − 2y … 30 + 2y` dans la moitié haute, symétriquement dans la moitié basse.

## Reste à établir

- Mélange alpha du jeu (**vérifié**, routine `0x0041DFC0`) : par composante de 5 bits,
  `(a × source + (31 − a) × fond) >> 5`. Non encore appliqué : `zeus-map` utilise le mélange
  8 bits de SDL.
- Rang des groupes : `image_id_from_group` reçoit `collection × 512 + rang`, le rang comptant
  les seuls groupes non vides du fichier, dans l'ordre (**vérifié** : voir
  `tools/annotate_groups.py`).
- Signification des types 1, 10, 12, 13, 20 (ils se décodent, mais leur rôle n'est pas établi).
- Champs inconnus listés ci-dessus, en particulier les octets 24 à 28 (position dans le bitmap
  source ?) et le champ 64.
