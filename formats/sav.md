# `.sav` — sauvegardes

Une sauvegarde est l'image d'une partie des variables globales du jeu, bloc par bloc. Le
conteneur est celui des cartes ([`map.md`](map.md)) ; ce document décrit la version écrite par
l'exécutable GOG (`Zeus.exe`, SHA-256 `382bcd28…75e90a57`) et chaque bloc. Les adresses sont
celles de cet exécutable. Tout est little-endian.

État au 2026-10-15 : les 139 blocs sont identifiés par leur adresse, leur forme et leur
compression ; leur contenu est établi pour une partie seulement (colonne *Statut*).

## Routines

| Adresse | Rôle | Statut |
|---|---|---|
| `0x0050E2C0` | écriture d'une sauvegarde (menu *Save adventure* ; `last.sav` chaque mois depuis `0x00483C10` si l'option est active) | vérifié |
| `0x0050D6E0` | lecture : appelle `0x0050CB80` pour chaque bloc ; les 15 premiers seulement si son second argument est non nul | vérifié |
| `0x0050CB80` | lecture d'un bloc, avec adaptation aux anciennes versions (voir plus bas) | vérifié |
| `0x0063C2D0` | table statique des blocs : 139 entrées de 20 octets | vérifié |
| `0x0050D180` | initialiseur statique (appelé par la table du CRT à `0x005E80A8`) : remplit les entrées 101 à 138 | vérifié |
| `0x0050D130` | remplit au démarrage les entrées dont l'adresse vaut `0x00F9C14C` (rangs 128 et 129) par les fonctions de la table `0x0063C2C8` | vérifié |
| `0x005C5F80` / `0x005C6020` | écriture / lecture d'un bloc compressé | vérifié |
| `0x005C96C0` / `0x005CA160` | PKWARE DCL `implode` / `explode` | vérifié |

Les cartes ont leurs propres tables (`0x0063CDC0`, 33 blocs, et deux variantes `0x0063D058`,
`0x0063D2F0`), écrites par `0x005118C0` et lues par `0x0050DF50`.

## Écriture

`0x0050E2C0` met la version à 321 (`0x141`, variable `0x00F9D940`), le nombre de blocs à 139
(`0x00F9D944`), recopie les 139 entrées de la table statique dans la table enregistrée
(`0x00F9C1D0`, 300 entrées, le reste à zéro), puis écrit chaque bloc dans l'ordre :
`taille × nombre` octets lus à l'adresse de l'entrée, bruts ou compressés selon l'indicateur.
Avant cela, elle libère les tableaux dynamiques des 232 éléments du rang 101 (pointeur à +4,
compteur à +0x138 remis à zéro) : la sauvegarde n'en contient que des pointeurs nuls. Selon son
second argument et la variable `0x00902F78`, le contenu de `parent.dat` (500 000 octets au
plus) suit le dernier bloc ; aucune sauvegarde étudiée n'en contient.

Une entrée de table (20 octets) : indicateur de compression, adresse, taille d'un élément,
nombre d'éléments, pointeur vers une liste de convertisseurs (non nul pour le seul rang 7).
L'adresse enregistrée est purement informative : la lecture utilise les adresses de la table
statique de l'exécutable qui lit.

## Compression

Un bloc compressé est un `u32` suivi de ce nombre d'octets de flux PKWARE DCL `implode`, en mode
binaire avec un dictionnaire de 4 096 octets (en-tête `00 06`). Le jeu appelle `implode`
(`0x005C96C0`) avec une structure de travail de 0x8DD8 octets prise sur la pile. Cas
particuliers, lus dans `0x005C5F80` et `0x005C6020` (vérifié) :

- un bloc de plus de 1 200 000 octets n'est pas écrit du tout (aucun ne l'est) ;
- longueur `0x80000000` : le bloc suit sans compression ;
- longueur 0 : le bloc est rempli de zéros à la lecture.

Le texte qui suit les données dans la structure de travail compte : en fin de flux, `implode`
compare des octets situés au-delà des données, c'est-à-dire ce que le bloc précédent a laissé
dans le tampon et, au-delà du tampon, la table `phash_offs` qui le suit dans la structure. Un
compresseur qui garde une seule structure pour toute la sauvegarde, zéro au départ, redonne les
flux du jeu à l'octet près (vérifié sur une sauvegarde de l'exe GOG et sur `tutorial00.sav` :
32 blocs compressés sur 32 ; avec une structure neuve à chaque bloc, 29 sur 32).

Le jeu embarque aussi zlib (`inflate 1.0.7`) ; les sauvegardes ne l'utilisent pas.

## Lecture et anciennes versions

Un bloc est identifié par son **rang**. `0x0050CB80` lit le bloc de rang *i* avec la forme
(taille d'élément, nombre) inscrite dans la table du fichier, puis le recopie vers la variable
de la table statique, élément par élément, en tronquant ou en complétant de zéros. Le rang 7
passe en plus par un convertisseur choisi selon la version du fichier. L'adresse `0x00F9C14E`
signifie « bloc à sauter ». Un fichier de version inférieure à 202 est refusé.

Les 16 didacticiels livrés (`Adventures/tutorial00.sav` à `tutorial15.sav`, identiques dans
les trois installations) sont en versions 235, 238 et 241, avec 138 blocs : le rang 138
n'existait pas et onze blocs avaient une autre forme (dernière colonne du tableau).

## Blocs

« Grille » : 51 984 cases (228 × 228), voir [`map.md`](map.md). Statut : vérifié (code ou
mesure cités), hypothèse (cohérent avec les comptages, à confirmer dans le code), inconnu.

| Rang | Adresse (GOG) | Élément × nombre | Compr. | Nom | Contenu | Statut | Versions 235–241 |
|---|---|---|---|---|---|---|---|
| 0 | `0x00F9D940` | 4 × 1 | non | `version` | version du format ; 321 pour l'exe GOG (`0x0050E2C0` écrit `0x141`) | vérifié | = |
| 1 | `0x00F9D944` | 4 × 1 | non | `block_count` | nombre de blocs écrits (139 ; 138 dans les sauvegardes livrées) | vérifié | = |
| 2 | `0x00F9C1D0` | 20 × 300 | non | `block_table` | table des blocs : 300 entrées de 20 octets, voir plus haut | vérifié | = |
| 3 | `0x00F2A7B2` | 1 × 1 | non | `flag_f2a7b2` | octet ; `0xFF` en début de partie | inconnu | = |
| 4 | `0x00FE6A58` | 19184 × 1 | oui | `city` | structure unique de 19 184 octets lue par 343 fonctions : état général de la ville | hypothèse | = |
| 5 | `0x0091EC04` | 4 × 1 | non | `year` | année, signée, négative avant notre ère (−1989 ↔ « Apr 1989 BC » à l'écran) | vérifié | = |
| 6 | `0x0091EC08` | 4 × 1 | non | `month` | mois 0 à 11 (3 ↔ « Apr ») ; `0x00483C10` passe à l'année suivante à 12 | vérifié | = |
| 7 | `0x008FBAC0` | 572 × 22 | oui | `cities` | 22 éléments ; seul bloc doté d'un convertisseur de version (`0x0063C2A8`) | hypothèse | 235 : 476 × 22; 238 : 476 × 22; 241 : 476 × 22 |
| 8 | `0x0065B9B0` | 4 × 36 | non | `table_65b9b0` | 36 entiers | inconnu | = |
| 9 | `0x00902928` | 2032 × 1 | non | `scenario` | description du scénario (texte d'accueil, objectifs) ; aussi dans les `.map` (rang 14) | hypothèse | = |
| 10 | `0x008FEBE8` | 72 × 200 | oui | `table_8febe8` | aussi dans les `.map` (rang 17) | inconnu | 235 : 40 × 200; 238 : 40 × 200; 241 : 40 × 200 |
| 11 | `0x006A9C9C` | 1 × 1 | non | `flag_6a9c9c` | octet | inconnu | = |
| 12 | `0x00F2A988` | 4 × 1 | non | `value_f2a988` | entier | inconnu | = |
| 13 | `0x009173F4` | 124 × 150 | non | `table_9173f4` | 150 × 124 octets ; avec le rang 14, forme le bloc 16 des `.map` (`0x9173F0`, 18 628 octets) | hypothèse | = |
| 14 | `0x009173F0` | 4 × 1 | non | `table_9173f4_count` | entier placé juste avant le rang 13 | hypothèse | = |
| 15 | `0x00D404B8` | 1 × 51984 | oui | `grid_edge` | grille u8 ; rang 5 des `.map` (bit `0x40` : case où l'image est dessinée) | hypothèse | = |
| 16 | `0x00D26E98` | 2 × 51984 | oui | `grid_building` | grille u16 : numéro du bâtiment de chaque case (maximum = nombre de bâtiments) | hypothèse | = |
| 17 | `0x00CCE128` | 4 × 51984 | oui | `grid_terrain` | grille u32 : drapeaux de terrain ; rang 6 des `.map` | hypothèse | = |
| 18 | `0x00CC1618` | 1 × 51984 | oui | `grid_cc1618` | grille u8 | inconnu | = |
| 19 | `0x00D0D878` | 2 × 51984 | oui | `grid_figure` | grille u16 : numéro de personnage de chaque case | hypothèse | = |
| 20 | `0x00D00D68` | 1 × 51984 | oui | `grid_bitfield` | grille u8 ; rang 7 des `.map` (taille − 1 d'un élément de plusieurs cases dans les 4 bits bas) | hypothèse | = |
| 21 | `0x00CB4B08` | 1 × 51984 | oui | `grid_cb4b08` | grille u8 | inconnu | = |
| 22 | `0x00CA7FF8` | 1 × 51984 | non | `grid_random` | grille u8 de valeurs aléatoires ; rang 8 des `.map` ; stockée sans compression | hypothèse | = |
| 23 | `0x00C9B4E8` | 1 × 51984 | oui | `grid_c9b4e8` | grille u8 | inconnu | = |
| 24 | `0x00C8E9D8` | 1 × 51984 | oui | `grid_c8e9d8` | grille u8 ; rang 9 des `.map` | inconnu | = |
| 25 | `0x00C753B8` | 2 × 51984 | oui | `grid_c753b8` | grille u16 | inconnu | = |
| 26 | `0x00B90C98` | 1 × 51984 | oui | `grid_b90c98` | grille u8 (aucune écriture directe dans le code) | inconnu | = |
| 27 | `0x00B84188` | 1 × 51984 | oui | `grid_b84188` | grille u8 (aucune écriture directe dans le code) | inconnu | = |
| 28 | `0x00DBD788` | 388 × 2000 | oui | `figures` | 2 000 personnages de 388 octets (marcheurs, animaux…) | hypothèse | = |
| 29 | `0x00EF4FE8` | 2 × 1000 | oui | `table_ef4fe8` | 1 000 × u16 | inconnu | = |
| 30 | `0x00E7AEC8` | 1 × 500000 | oui | `buffer_e7aec8` | 500 000 octets | inconnu | = |
| 31 | `0x00DB7B40` | 156 × 100 | oui | `table_db7b40` | 100 × 156 octets | inconnu | = |
| 32 | `0x00F9C148` | 4 × 1 | non | `padding` | emplacement vide (`0x00F9C148`, jamais référencé par le code) | vérifié | = |
| 33 | `0x00FE6910` | 1 × 1 | non | `flag_fe6910` | octet | inconnu | 235 : 4 × 1; 238 : 4 × 1 |
| 34 | `0x00FE6A14` | 32 × 2 | non | `names` | 2 × 32 octets : nom du profil, nom de la ville | hypothèse | = |
| 35 | `0x0092E658` | 280 × 4000 | oui | `buildings` | 4 000 bâtiments de 280 octets | hypothèse | = |
| 36 | `0x00FA05E8` | 4 × 1 | non | `value_fa05e8` | entier | inconnu | = |
| 37 | `0x0091EC14` | 4 × 1 | non | `value_91ec14` | entier | inconnu | = |
| 38 | `0x0091EC0C` | 4 × 1 | non | `month_tick` | tour dans le mois, 0 à 15 (`0x00483C10` passe au mois suivant à 16) | vérifié | = |
| 39 | `0x0091EC10` | 4 × 1 | non | `tick` | compteur de tours, incrémenté à chaque appel de `0x00483C10` | vérifié | = |
| 40 | `0x0091EBF4` | 4 × 1 | non | `buildings_high` | plus grand numéro de bâtiment utilisé | hypothèse | = |
| 41 | `0x00F9C148` | 4 × 1 | non | `padding` | emplacement vide (`0x00F9C148`) | vérifié | = |
| 42 | `0x00F137E4` | 4 × 1 | non | `random_state_1` | entier qui change à chaque tirage (165 écritures) ; rang 10 des `.map` | hypothèse | = |
| 43 | `0x00F137E0` | 4 × 1 | non | `random_state_2` | second entier du générateur ; rang 11 des `.map` | hypothèse | = |
| 44 | `0x00DA2F64` | 4 × 1 | non | `value_da2f64` | entier ; rang 12 des `.map` | inconnu | = |
| 45 | `0x00DA2F68` | 4 × 1 | non | `value_da2f68` | entier ; rang 13 des `.map` | inconnu | = |
| 46 | `0x00665E30` | 4 × 1 | non | `value_665e30` | entier | inconnu | = |
| 47 | `0x00FE69E8` | 4 × 1 | non | `value_fe69e8` | entier, jamais référencé par le code | inconnu | = |
| 48 | `0x008F9838` | 4 × 1 | non | `value_8f9838` | entier | inconnu | = |
| 49 | `0x008F9834` | 4 × 1 | non | `value_8f9834` | entier | inconnu | = |
| 50 | `0x00903120` | 4 × 1 | non | `value_903120` | entier | inconnu | = |
| 51 | `0x0091EB58` | 4 × 36 | non | `table_91eb58` | 36 entiers | inconnu | = |
| 52 | `0x0091EAC8` | 4 × 36 | non | `table_91eac8` | 36 entiers | inconnu | = |
| 53 | `0x0091EC00` | 4 × 1 | non | `value_91ec00` | entier | inconnu | = |
| 54 | `0x00FD3838` | 60 × 1000 | oui | `table_fd3838` | 1 000 × 60 octets | inconnu | = |
| 55 | `0x00FD37F4` | 4 × 1 | non | `value_fd37f4` | entier | inconnu | = |
| 56 | `0x00FD37F0` | 4 × 1 | non | `value_fd37f0` | entier | inconnu | = |
| 57 | `0x00FD37EC` | 4 × 1 | non | `value_fd37ec` | entier | inconnu | = |
| 58 | `0x00FD37C4` | 1 × 10 | non | `table_fd37c4` | 10 octets | inconnu | = |
| 59 | `0x00FD3774` | 4 × 20 | non | `table_fd3774` | 20 entiers | inconnu | = |
| 60 | `0x00FD3724` | 4 × 20 | non | `table_fd3724` | 20 entiers | inconnu | = |
| 61 | `0x0091EC44` | 4 × 1 | non | `value_91ec44` | entier | inconnu | = |
| 62 | `0x0091EC40` | 4 × 1 | non | `value_91ec40` | entier | inconnu | = |
| 63 | `0x00EF57F0` | 4 × 1 | non | `value_ef57f0` | entier | inconnu | = |
| 64 | `0x00F9C148` | 4 × 1 | non | `padding` | emplacement vide (`0x00F9C148`) | vérifié | = |
| 65 | `0x00F2A7C0` | 4 × 1 | non | `value_f2a7c0` | entier | inconnu | = |
| 66 | `0x00F2A7C4` | 4 × 1 | non | `value_f2a7c4` | entier | inconnu | = |
| 67 | `0x007773D0` | 128 × 70 | non | `table_7773d0` | 70 × 128 octets | inconnu | = |
| 68 | `0x0091EBF8` | 4 × 1 | non | `buildings_count` | nombre de bâtiments | hypothèse | = |
| 69 | `0x008F9860` | 88 × 100 | non | `table_8f9860` | 100 × 88 octets | inconnu | = |
| 70 | `0x008F9858` | 4 × 1 | non | `table_8f9860_count` | entier | inconnu | = |
| 71 | `0x0091F1D0` | 2 × 500 | oui | `table_91f1d0` | 500 × u16 | inconnu | = |
| 72 | `0x0091EDE8` | 2 × 500 | oui | `table_91ede8` | 500 × u16, jamais référencé par le code | inconnu | = |
| 73 | `0x0091F5B8` | 2 × 4000 | oui | `table_91f5b8` | 4 000 × u16 | inconnu | = |
| 74 | `0x0091EC70` | 4 × 1 | non | `value_91ec70` | entier | inconnu | = |
| 75 | `0x0091EC68` | 4 × 1 | non | `value_91ec68` | entier | inconnu | = |
| 76 | `0x00EF57D4` | 4 × 1 | non | `value_ef57d4` | entier | inconnu | = |
| 77 | `0x00EF57D0` | 4 × 1 | non | `value_ef57d0` | entier | inconnu | = |
| 78 | `0x00EF57CC` | 4 × 1 | non | `value_ef57cc` | entier | inconnu | = |
| 79 | `0x00EF57C8` | 4 × 1 | non | `value_ef57c8` | entier | inconnu | = |
| 80 | `0x00EF57C4` | 4 × 1 | non | `value_ef57c4` | entier | inconnu | = |
| 81 | `0x009214F8` | 268 × 200 | non | `table_9214f8` | 200 × 268 octets | inconnu | = |
| 82 | `0x00DB7B30` | 4 × 1 | non | `value_db7b30` | entier | inconnu | = |
| 83 | `0x0091EBFC` | 4 × 1 | non | `value_91ebfc` | entier | inconnu | = |
| 84 | `0x0102D1CC` | 4 × 1 | non | `value_102d1cc` | entier, jamais référencé par le code | inconnu | = |
| 85 | `0x0102D1C8` | 4 × 1 | non | `value_102d1c8` | entier | inconnu | = |
| 86 | `0x0102D1C4` | 4 × 1 | non | `value_102d1c4` | entier | inconnu | = |
| 87 | `0x0102D1C0` | 4 × 1 | non | `value_102d1c0` | entier, jamais référencé par le code | inconnu | = |
| 88 | `0x00DA2FB8` | 4 × 1 | non | `value_da2fb8` | entier | inconnu | = |
| 89 | `0x00DA2FBC` | 4 × 1 | non | `value_da2fbc` | entier | inconnu | = |
| 90 | `0x00DA2FC0` | 4 × 1 | non | `value_da2fc0` | entier | inconnu | = |
| 91 | `0x00DA2FC4` | 4 × 1 | non | `value_da2fc4` | entier | inconnu | = |
| 92 | `0x008F9828` | 2 × 1 | non | `value_8f9828` | u16 | inconnu | = |
| 93 | `0x0091EA44` | 4 × 1 | non | `value_91ea44` | entier | inconnu | = |
| 94 | `0x0091EA40` | 4 × 1 | non | `value_91ea40` | entier | inconnu | = |
| 95 | `0x00F2A258` | 65 × 1 | non | `text_f2a258` | 65 octets, chaîne | inconnu | = |
| 96 | `0x00FA05C0` | 8 × 4 | non | `table_fa05c0` | 4 × 8 octets | inconnu | = |
| 97 | `0x00DA23A8` | 4 × 1 | non | `value_da23a8` | entier | inconnu | = |
| 98 | `0x00DA23A4` | 4 × 1 | non | `value_da23a4` | entier | inconnu | = |
| 99 | `0x00C35C68` | 1 × 51984 | non | `grid_c35c68` | grille u8 ; rang 15 des `.map` | inconnu | = |
| 100 | `0x00EF57D8` | 4 × 4 | non | `table_ef57d8` | 4 entiers, jamais référencés par le code | inconnu | = |
| 101 | `0x009034F8` | 324 × 232 | oui | `table_9034f8` | 232 × 324 octets ; le pointeur à +4 de chaque élément est libéré et remis à 0 avant l'écriture | hypothèse | = |
| 102 | `0x00C1C648` | 1 × 51984 | non | `grid_c1c648` | grille u8, stockée sans compression | inconnu | = |
| 103 | `0x00B1E8D8` | 32 × 1 | non | `value_b1e8d8` | 32 octets | inconnu | = |
| 104 | `0x00699570` | 36 × 1 | oui | `value_699570` | 36 octets | inconnu | = |
| 105 | `0x00B1E8F8` | 4 × 51984 | oui | `grid_b1e8f8` | grille u32 | inconnu | = |
| 106 | `0x006A9CBC` | 1 × 26 | non | `table_6a9cbc` | 26 octets | inconnu | = |
| 107 | `0x0069DFC8` | 1 × 13 | non | `table_69dfc8` | 13 octets | inconnu | = |
| 108 | `0x00C688A8` | 1 × 51984 | oui | `grid_c688a8` | grille u8 | inconnu | = |
| 109 | `0x00C29158` | 1 × 51984 | oui | `grid_c29158` | grille u8 | inconnu | = |
| 110 | `0x00B1E8B8` | 4 × 1 | non | `value_b1e8b8` | entier | inconnu | = |
| 111 | `0x008E01FA` | 1 × 51984 | oui | `grid_8e01fa` | grille u8 | inconnu | = |
| 112 | `0x008ECD0A` | 1 × 51984 | oui | `grid_8ecd0a` | grille u8 | inconnu | = |
| 113 | `0x00EF5800` | 4 × 367 | oui | `table_ef5800` | 367 entiers | inconnu | 235 : 4 × 334; 238 : 4 × 334; 241 : 4 × 334 |
| 114 | `0x0090311C` | 4 × 1 | non | `value_90311c` | entier | inconnu | = |
| 115 | `0x0078C578` | 28 × 8 | non | `table_78c578` | 8 × 28 octets | inconnu | = |
| 116 | `0x00665CC8` | 1 × 75 | non | `table_665cc8` | 75 octets | inconnu | 235 : 1 × 40; 238 : 1 × 40; 241 : 1 × 40 |
| 117 | `0x00FE690C` | 4 × 1 | non | `value_fe690c` | entier | inconnu | = |
| 118 | `0x00FE6908` | 1 × 1 | non | `flag_fe6908` | octet | inconnu | = |
| 119 | `0x00781A74` | 4 × 900 | non | `table_781a74` | 900 entiers | inconnu | = |
| 120 | `0x00EF5DD8` | 52 × 14 | non | `table_ef5dd8` |  | inconnu | 235 : 40 × 12; 238 : 40 × 12; 241 : 52 × 12 |
| 121 | `0x00DB68C0` | 8 × 32 | non | `table_db68c0` | 32 × 8 octets | inconnu | = |
| 122 | `0x00FFA450` | 300 × 1 | non | `table_ffa450` | 300 octets | inconnu | 235 : 224 × 1; 238 : 224 × 1; 241 : 224 × 1 |
| 123 | `0x00FE43F0` | 32 × 18 | non | `table_fe43f0` |  | inconnu | 235 : 12 × 14; 238 : 12 × 14; 241 : 12 × 14 |
| 124 | `0x00F0E4E8` | 28 × 8 | non | `table_f0e4e8` |  | inconnu | 235 : 28 × 6; 238 : 28 × 6; 241 : 28 × 6 |
| 125 | `0x00916AF8` | 200 × 1 | non | `status_text` | 200 octets : dernier message du panneau (« The city needs more workers. ») | hypothèse | = |
| 126 | `0x00EF60B0` | 1 × 221 | non | `table_ef60b0` | 221 octets | inconnu | 235 : 1 × 199; 238 : 1 × 199; 241 : 1 × 199 |
| 127 | `0x00FE50D4` | 1 × 17 | non | `table_fe50d4` | 17 octets | inconnu | = |
| 128 | `0x00FE2770` | 76 × 6 | non | `table_fe2770` | 6 × 76 octets ; emplacement rempli au démarrage par `0x0063C2C8[0]` | inconnu | = |
| 129 | `0x00FE276C` | 4 × 1 | non | `value_fe276c` | entier ; emplacement rempli au démarrage par `0x0063C2C8[1]` | inconnu | = |
| 130 | `0x00665C7C` | 1 × 75 | non | `table_665c7c` | 75 octets | inconnu | 235 : 1 × 40; 238 : 1 × 40; 241 : 1 × 40 |
| 131 | `0x0063A888` | 4 × 1 | non | `value_63a888` | entier | inconnu | = |
| 132 | `0x00665E48` | 112 × 4 | non | `table_665e48` | 4 × 112 octets | inconnu | = |
| 133 | `0x007796D0` | 4 × 1 | non | `value_7796d0` | entier | inconnu | = |
| 134 | `0x007796D4` | 16 × 2000 | non | `table_7796d4` | 2 000 × 16 octets | inconnu | = |
| 135 | `0x00F135E8` | 4 × 1 | non | `value_f135e8` | entier | inconnu | = |
| 136 | `0x00F13480` | 36 × 10 | non | `table_f13480` | 10 × 36 octets | inconnu | = |
| 137 | `0x00F2A990` | 1 × 1 | non | `flag_f2a990` | octet | inconnu | = |
| 138 | `0x006AA4E0` | 4 × 1 | non | `value_6aa4e0` | entier | inconnu | 235 : absent; 238 : absent; 241 : absent |

### Observations utilisées

- Sauvegarde faite dans le premier didacticiel de Poséidon dès son ouverture (capture : avril
  1989 av. J.-C., tentes et quelques marcheurs) : rang 5 = −1989, rang 6 = 3,
  rangs 40 et 68 = 13 ; 13 éléments occupés au rang 35 (numéros 1 à 13), dont 11 de type 2 à
  +0x10 (les tentes, hypothèse) ; 10 au rang 28.
- `tutorial15.sav` (*See a working city*) : 629 éléments occupés au rang 35, rangs 40 et 68 = 629 ;
  297 au rang 28. `map.md` relevait déjà un maximum de 629 dans la grille u16 du rang 16.
- Bâtiment : abscisse et ordonnée sur deux octets à +6 et +8, indice de case
  `228 × (y + 44) + x + 44` à +12 (vérifié sur les 13 bâtiments ; le décalage de 44 reste à
  expliquer).

## Fichiers testés

- `original/*/Adventures/tutorial*.sav` : 48 fichiers (16 distincts), lus en entier ;
- sauvegardes faites avec `zeus-run --deterministic` dans `build/essais/` (non publiées).

## À faire

Champs des bâtiments (rang 35), des personnages (rang 28) et de l'état de la ville (rang 4) ;
rôle des grilles inconnues ; tables moyennes (rangs 10, 13, 31, 54, 67, 81, 101, 134).
