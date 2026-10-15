# `.sav` — sauvegardes

Une sauvegarde est l'image d'une partie des variables globales du jeu, bloc par bloc. Le
conteneur est celui des cartes ([`map.md`](map.md)) ; ce document décrit la version écrite par
l'exécutable GOG (`Zeus.exe`, SHA-256 `382bcd28…75e90a57`) et chaque bloc. Les adresses sont
celles de cet exécutable. Tout est little-endian.

Outil : `tools/savefile.py` (`info`, `check`, `dump`, `samples`) lit toutes les sauvegardes
et les réécrit à l'octet près, compression comprise.

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
flux du jeu à l'octet près : `tools/savefile.py check` réécrit les 219 sauvegardes étudiées
(7 008 blocs compressés) à l'identique. Avec une structure neuve à chaque bloc, 3 blocs sur 32
diffèrent déjà dans une seule sauvegarde.

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
| 4 | `0x00FE6A58` | 19184 × 1 | oui | `city` | état général de la ville, lu par 343 fonctions ; +0x0C trésor (i32), +0x24 population (i32), voir Observations | vérifié (2 champs) ; reste inconnu | = |
| 5 | `0x0091EC04` | 4 × 1 | non | `year` | année, signée, négative avant notre ère (−1989 ↔ « Apr 1989 BC » à l'écran) | vérifié | = |
| 6 | `0x0091EC08` | 4 × 1 | non | `month` | mois 0 à 11 (3 ↔ « Apr ») ; `0x00483C10` passe à l'année suivante à 12 | vérifié | = |
| 7 | `0x008FBAC0` | 572 × 22 | oui | `cities` | 22 éléments ; seul bloc doté d'un convertisseur de version (`0x0063C2A8`) | hypothèse | 235 : 476 × 22; 238 : 476 × 22; 241 : 476 × 22 |
| 8 | `0x0065B9B0` | 4 × 36 | non | `table_65b9b0` | 36 entiers | inconnu | = |
| 9 | `0x00902928` | 2032 × 1 | non | `scenario` | description du scénario (texte d'accueil, objectifs) ; aussi dans les `.map` (rang 14) | hypothèse | = |
| 10 | `0x008FEBE8` | 72 × 200 | oui | `table_8febe8` | aussi dans les `.map` (rang 17) | inconnu | 235 : 40 × 200; 238 : 40 × 200; 241 : 40 × 200 |
| 11 | `0x006A9C9C` | 1 × 1 | non | `flag_6a9c9c` | octet | inconnu | = |
| 12 | `0x00F2A988` | 4 × 1 | non | `value_f2a988` | entier | inconnu | = |
| 13 | `0x009173F4` | 124 × 150 | non | `scenario_events` | 150 enregistrements numérotés (+0 = rang), avec type à +2 et de nombreux champs à −1 : événements programmés de l'aventure ; avec le rang 14, forme le bloc 16 des `.map` (`0x9173F0`, 18 628 octets) | hypothèse | = |
| 14 | `0x009173F0` | 4 × 1 | non | `scenario_events_count` | entier placé juste avant le rang 13 (3 dans `tutorial15.sav`) | hypothèse | = |
| 15 | `0x00D404B8` | 1 × 51984 | oui | `grid_edge` | grille u8 ; rang 5 des `.map` (bit `0x40` : case où l'image est dessinée) | hypothèse | = |
| 16 | `0x00D26E98` | 2 × 51984 | oui | `grid_building` | grille u16 : numéro du bâtiment qui occupe la case | vérifié (613 bâtiments sur 628 de la grande ville retrouvés à leur case +0x0C, 13 sur 13 dans le didacticiel) | = |
| 17 | `0x00CCE128` | 4 × 51984 | oui | `grid_terrain` | grille u32 : drapeaux de terrain ; rang 6 des `.map` ; une route posée met le bit `0x40` sur ses cases | vérifié (route) | = |
| 18 | `0x00CC1618` | 1 × 51984 | oui | `grid_cc1618` | grille u8 | inconnu | = |
| 19 | `0x00D0D878` | 2 × 51984 | oui | `grid_figure` | grille u16 : premier personnage de la case ; les suivants sont chaînés par +0x08 du personnage (`0x004DC860`) | vérifié | = |
| 20 | `0x00D00D68` | 1 × 51984 | oui | `grid_bitfield` | grille u8 ; rang 7 des `.map` (taille − 1 d'un élément de plusieurs cases dans les 4 bits bas) | hypothèse | = |
| 21 | `0x00CB4B08` | 1 × 51984 | oui | `grid_cb4b08` | grille u8, non nulle seulement sous des bâtiments (141 cases dans la grande ville) ; `0x0041DF90` y fait évoluer des états 0x0F → 0x13 pour le type de bâtiment 199 | inconnu | = |
| 22 | `0x00CA7FF8` | 1 × 51984 | non | `grid_random` | grille u8 de valeurs aléatoires ; rang 8 des `.map` ; stockée sans compression | hypothèse | = |
| 23 | `0x00C9B4E8` | 1 × 51984 | oui | `grid_desirability` | grille i8 bornée à −100…100 : `0x004A4C60` ajoute autour d'un bâtiment une valeur par anneau, rayons 1 à 6 (tables `0x005F079E`…) ; attrait des cases | hypothèse | = |
| 24 | `0x00C8E9D8` | 1 × 51984 | oui | `grid_c8e9d8` | grille u8 ; rang 9 des `.map` | inconnu | = |
| 25 | `0x00C753B8` | 2 × 51984 | oui | `grid_c753b8` | grille u16 | inconnu | = |
| 26 | `0x00B90C98` | 1 × 51984 | oui | `grid_b90c98` | grille u8 : copie du rang 18 faite par `0x0049AF50` (tracé de murs) avec celle des drapeaux de terrain (`0x00B9D7A8`, non sauvegardée) ; aucune autre écriture | vérifié (copie) ; usage hypothèse | = |
| 27 | `0x00B84188` | 1 × 51984 | oui | `grid_b84188` | grille u8 (aucune écriture directe dans le code) | inconnu | = |
| 28 | `0x00DBD788` | 388 × 2000 | oui | `figures` | personnages (marcheurs, animaux…), numérotés à partir de 1 ; l'élément 0 sert de brouillon aux calculs de trajet ; créés par `0x004D9FD0` ; la liste des numéros libres (`0x00DBB84C`) n'est pas sauvegardée, `0x004D9F40` la reconstruit au chargement ; champs plus bas | vérifié | = |
| 29 | `0x00EF4FE8` | 2 × 1000 | oui | `table_ef4fe8` | 1 000 × u16 | inconnu | = |
| 30 | `0x00E7AEC8` | 1 × 500000 | oui | `buffer_e7aec8` | 500 000 octets | inconnu | = |
| 31 | `0x00DB7B40` | 156 × 100 | oui | `figure_groups` | groupes de personnages : +8 huit numéros de personnage (i16), +0x78 numéro de bâtiment ; `0x004FFFD0` renvoie les huit personnages (état 4 à +0x38) et libère le groupe quand le bâtiment n'existe plus | hypothèse | = |
| 32 | `0x00F9C148` | 4 × 1 | non | `padding` | emplacement vide (`0x00F9C148`, jamais référencé par le code) | vérifié | = |
| 33 | `0x00FE6910` | 1 × 1 | non | `flag_fe6910` | octet | inconnu | 235 : 4 × 1; 238 : 4 × 1 |
| 34 | `0x00FE6A14` | 32 × 2 | non | `names` | 2 × 32 octets : nom du profil, nom de la ville | hypothèse | = |
| 35 | `0x0092E658` | 280 × 4000 | oui | `buildings` | bâtiments, numérotés à partir de 1 (l'élément 0 ne sert pas) ; créés par `0x00493E70`, effacés par `0x00493E50` ; champs plus bas | vérifié | = |
| 36 | `0x00FA05E8` | 4 × 1 | non | `value_fa05e8` | entier | inconnu | = |
| 37 | `0x0091EC14` | 4 × 1 | non | `cycle_step` | étape 0 à 50 du cycle de `0x00483840`, appelé à chaque tour ; `0x00483C10` à la fin du cycle (`docs/engine/terrain.md`, section 6) | vérifié | = |
| 38 | `0x0091EC0C` | 4 × 1 | non | `month_tick` | tour dans le mois, 0 à 15 (`0x00483C10` passe au mois suivant à 16) | vérifié | = |
| 39 | `0x0091EC10` | 4 × 1 | non | `tick` | compteur de tours, incrémenté à chaque appel de `0x00483C10` | vérifié | = |
| 40 | `0x0091EBF4` | 4 × 1 | non | `buildings_high` | plus grand numéro de bâtiment jamais occupé (`0x004889F0`) | vérifié | = |
| 41 | `0x00F9C148` | 4 × 1 | non | `padding` | emplacement vide (`0x00F9C148`) | vérifié | = |
| 42 | `0x00F137E4` | 4 × 1 | non | `random_state_1` | générateur aléatoire, premier registre à décalage de 31 bits (`0x004D9FD0` : 31 pas par tirage, rétroaction bit 4 ⊕ bit 0 vers le bit 30) ; rang 10 des `.map` | vérifié | = |
| 43 | `0x00F137E0` | 4 × 1 | non | `random_state_2` | second registre du générateur, même règle ; rang 11 des `.map` | vérifié | = |
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
| 54 | `0x00FD3838` | 60 × 1000 | oui | `messages` | messages reçus, créés par `0x00521A70` : +0 et +4 arguments, +8 année (i16), +0xA type de message (i16, indice dans la table `0x00F13CB2`, 0x50 octets par type), +0xC numéro (rang 55), +0xF mois, +0x10 dix u16 de paramètres (`0xFFFF` sans paramètre) | vérifié | = |
| 55 | `0x00FD37F4` | 4 × 1 | non | `messages_created` | compteur de messages, recopié à +0xC de chaque message | vérifié | = |
| 56 | `0x00FD37F0` | 4 × 1 | non | `messages_count` | incrémenté à chaque message reçu | vérifié | = |
| 57 | `0x00FD37EC` | 4 × 1 | non | `messages_last` | indice du dernier message reçu | vérifié | = |
| 58 | `0x00FD37C4` | 1 × 10 | non | `table_fd37c4` | 10 octets | inconnu | = |
| 59 | `0x00FD3774` | 4 × 20 | non | `table_fd3774` | 20 entiers | inconnu | = |
| 60 | `0x00FD3724` | 4 × 20 | non | `table_fd3724` | 20 entiers | inconnu | = |
| 61 | `0x0091EC44` | 4 × 1 | non | `value_91ec44` | entier | inconnu | = |
| 62 | `0x0091EC40` | 4 × 1 | non | `value_91ec40` | entier | inconnu | = |
| 63 | `0x00EF57F0` | 4 × 1 | non | `figures_created` | nombre de personnages créés ; chaque création le recopie à +0x86 puis l'incrémente (`0x004D9FD0`) | vérifié | = |
| 64 | `0x00F9C148` | 4 × 1 | non | `padding` | emplacement vide (`0x00F9C148`) | vérifié | = |
| 65 | `0x00F2A7C0` | 4 × 1 | non | `value_f2a7c0` | entier | inconnu | = |
| 66 | `0x00F2A7C4` | 4 × 1 | non | `value_f2a7c4` | entier | inconnu | = |
| 67 | `0x007773D0` | 128 × 70 | non | `table_7773d0` | lue et écrite par trois fonctions seulement (`0x0042E3D0`, `0x0042E560`) qui choisissent un son parmi 8 par élément et le jouent (`0x005A3D00`) toutes les 2 000 unités de `0x00F9C0F4` : sons de la ville | hypothèse | = |
| 68 | `0x0091EBF8` | 4 × 1 | non | `buildings_last` | plus grand numéro de bâtiment occupé, recalculé par `0x004889F0` | vérifié | = |
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
| 81 | `0x009214F8` | 268 × 200 | non | `storages` | réserves des bâtiments de stockage (types 0x39 à 0x3E), créées par `0x00494960` : +4 numéro de bâtiment, +8 occupé, puis 23 marchandises avec leurs capacités (3 200 par défaut) ; numérotées à partir de 1 | hypothèse | = |
| 82 | `0x00DB7B30` | 4 × 1 | non | `value_db7b30` | entier | inconnu | = |
| 83 | `0x0091EBFC` | 4 × 1 | non | `buildings_created` | nombre de bâtiments créés ; chaque création le recopie à +0x16 puis l'incrémente (`0x00493E70`) | vérifié | = |
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
| 99 | `0x00C35C68` | 1 × 51984 | non | `grid_meadow` | grille u8 ; rang 15 des `.map` ; valeur des cases de pré (terrain `0x800`) calculée par `0x004AA190`, 0 ailleurs | vérifié (calcul) ; sens hypothèse | = |
| 100 | `0x00EF57D8` | 4 × 4 | non | `table_ef57d8` | 4 entiers, jamais référencés par le code | inconnu | = |
| 101 | `0x009034F8` | 324 × 232 | oui | `table_9034f8` | 232 × 324 octets ; le pointeur à +4 de chaque élément est libéré et remis à 0 avant l'écriture | hypothèse | = |
| 102 | `0x00C1C648` | 1 × 51984 | non | `grid_regrowth` | grille u8, 255 sur presque toute la carte ; `0x004A7D30` remonte de 15 ou 16 (tirage) la valeur des cases de terrain `0x800` et `0x40000` jusqu'à 255 : repousse d'une ressource du terrain | hypothèse | = |
| 103 | `0x00B1E8D8` | 32 × 1 | non | `value_b1e8d8` | 32 octets ; +0x18 à +0x1D : indicateurs de la repousse (arbres, roseaux, prés présents ; arbre atteint par une scierie), remis à zéro par `0x004A7D30` | vérifié (+0x18 à +0x1D) ; reste inconnu | = |
| 104 | `0x00699570` | 36 × 1 | oui | `value_699570` | 36 octets | inconnu | = |
| 105 | `0x00B1E8F8` | 4 × 51984 | oui | `grid_b1e8f8` | grille u32 | inconnu | = |
| 106 | `0x006A9CBC` | 1 × 26 | non | `table_6a9cbc` | 26 octets | inconnu | = |
| 107 | `0x0069DFC8` | 1 × 13 | non | `table_69dfc8` | 13 octets | inconnu | = |
| 108 | `0x00C688A8` | 1 × 51984 | oui | `grid_c688a8` | grille u8 | inconnu | = |
| 109 | `0x00C29158` | 1 × 51984 | oui | `grid_c29158` | grille u8 recalculée par zone par `0x004A8E10` : 0 sur les cases de terrain `0x50004`, sinon valeur de `0x004A8C90` pour les cases de terrain `0x80` | vérifié (calcul) ; sens inconnu | = |
| 110 | `0x00B1E8B8` | 4 × 1 | non | `value_b1e8b8` | entier | inconnu | = |
| 111 | `0x008E01FA` | 1 × 51984 | oui | `grid_8e01fa` | grille u8 | inconnu | = |
| 112 | `0x008ECD0A` | 1 × 51984 | oui | `grid_8ecd0a` | grille u8 effacée puis recalculée entièrement par `0x0045ED20` selon l'orientation de la vue (`0x00DA2F88`) : donnée d'affichage dérivée | vérifié (recalcul) | = |
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

### Bâtiment (rang 35, 280 octets)

D'après la création `0x00493E70(type, x, y, taille, numéro)`, qui prend le premier numéro libre
quand `numéro` vaut 0 ; recoupé avec les 13 bâtiments du premier didacticiel.

| Offset | Type | Champ | Statut |
|---|---|---|---|
| +0x00 | u8 | état : 0 = libre, 3 à la création, 1 dans une ville en cours | vérifié |
| +0x01 | u8 | 1 à la création | hypothèse |
| +0x03 | u8 | valeur de la table `0x005F6368` (32 octets par type) | vérifié |
| +0x05 | u8 | 1 pour les types 2 à 14 | vérifié |
| +0x06, +0x08 | i16 | x, y | vérifié |
| +0x0C | i32 | indice de case : `0x00DA2F80 + 228 × y + x` (`0x00DA2F80` n'est pas sauvegardé ; il vaut 10 076 = 44 × 229 dans le didacticiel) | vérifié |
| +0x10 | i16 | type de bâtiment (2 pour les 11 tentes du didacticiel) | vérifié |
| +0x12 | i16 | type − 2 pour les types 2 à 14 ; valeur propre à quelques types | vérifié |
| +0x16 | i16 | numéro de création (rang 83) | vérifié |
| +0x34 | u8 | octet de la grille aléatoire (rang 22) de la case, `& 6` | vérifié |
| +0x43 | u8 | code fixé par type (0 à 0x16) | vérifié |
| +0x45 | u8 | type (octet bas) | vérifié |
| +0x4C | u8 | valeur de la table `0x005F636C` | vérifié |
| +0x4D | u8 | octet de la grille aléatoire de la case, `& 0x7F` | vérifié |
| +0xD0 | u8 | résultat de `0x004A4500(x, y, +0x03)` | vérifié |
| +0xD2 | u8 | 50 à la création | vérifié |
| +0xDA | u8 | drapeau selon le type (`0x005344A0`) | vérifié |

### Personnage (rang 28, 388 octets)

D'après la création `0x004D9FD0(a, type, x, y, b, placer)`, qui prend un numéro dans la liste
des numéros libres et remet l'élément à zéro.

| Offset | Type | Champ | Statut |
|---|---|---|---|
| +0x08 | u16 | personnage suivant sur la même case (tête de liste : grille du rang 19) | vérifié |
| +0x0A | u8 | type de personnage | vérifié |
| +0x0C | u8 | 0 à la création | vérifié |
| +0x0D | u8 | 1 à la création | vérifié |
| +0x0E | u8 | état : 0 = libre, 1 = actif ; 2 vu dans `0x004DDCB0` | vérifié |
| +0x0F | u8 | premier argument de la création | vérifié |
| +0x11 | u8 | cinquième argument de la création | vérifié |
| +0x14, +0x16 | i16 | x, y de la case | vérifié |
| +0x18, +0x1A | i16 | x, y (copie, à la création) | vérifié |
| +0x20 | i32 | indice de case : `0x00DA2F80 + 228 × y + x` | vérifié |
| +0x24, +0x26 | i16 | x, y (copie, à la création) | vérifié |
| +0x2C, +0x2E | i16 | x, y (copie, à la création) | vérifié |
| +0x38 | u8 | action en cours : mise à 4 quand le groupe du rang 31 disparaît, à 9 dans `0x004CFEE0` | vérifié (écritures) ; sens hypothèse |
| +0x39 | u8 | 15 à la création | vérifié |
| +0x4A, +0x4C | i16 | x × 15, y × 15 : position fine | vérifié |
| +0x5A | i16 | numéro de bâtiment (lu par `0x004CFEE0`) | vérifié |
| +0x7C, +0x7E | u8 | tirage aléatoire `& 3` à la création | vérifié |
| +0x86 | i16 | numéro de création (rang 63) | vérifié |
| +0x92 | u8 | 0 à la création | vérifié |
| +0x132 | u16 | `0xFFFF` à la création | vérifié |

### Variables recalculées au chargement

Utilisées partout avec les blocs mais absentes de la sauvegarde : `0x00DA2F80` (décalage ajouté
à `228 × y + x` dans tous les indices de case), `0x00DA2F78` et `0x00DA2F7C` (bornes de la zone
jouable, `0x004A8E10`), `0x00DA2F88` (orientation de la vue : 0, 2, 4 ou 6, `0x0045ED20`), la
liste des numéros de personnage libres (`0x00DBB84C`, `0x004D9F40`) et `0x0091EBF0` (dernière
réserve occupée du rang 81, `0x004889F0`).

### Grilles dans la grande ville

Cases non nulles, dont sous un bâtiment (rang 16) et sur une route (bit `0x40` du rang 17) :

| Rang | Non nulles | Sous bâtiment | Sur route | Valeurs fréquentes |
|---|---|---|---|---|
| 15 | 9 832 | 1 596 | 461 | 64, 1, 9, 72 |
| 20 | 5 647 | 1 338 | 182 | 32, 1, 33, 3 |
| 21 | 141 | 141 | 0 | 6, 5, 1, 7 |
| 23 | 4 842 | 1 707 | 425 | 254 (−2), 10, 2, 12 |
| 27 | 141 | 141 | 0 | 6, 5, 2, 1 |
| 99 | 573 | 94 | 15 | 8, 90, 66, 82 |
| 102 | 51 976 | 1 781 | 461 | 255 (51 826 cases) |
| 105 | 426 | 129 | 413 | 48, 56, 3 072, 3 120 |
| 108 | 126 | 96 | 5 | 82, 78, 121, 79 |
| 109 | 1 984 | 169 | 32 | 8, 16, 24, 96 |
| 111 | 7 202 | 1 629 | 345 | 1, 2, 4, 3 |
| 112 | 6 834 | 1 629 | 345 | 1, 2, 4, 3 |

Les rangs 18, 24, 25 et 26 sont entièrement nuls dans les 8 sauvegardes d'essai et les 16 didacticiels livrés.

### Observations

- Premier didacticiel de Poséidon dès son ouverture (capture : avril 1989 av. J.-C., tentes et
  quelques marcheurs) : rang 5 = −1989, rang 6 = 3 ; bâtiments 1 à 13 occupés, rangs 40, 68 et
  83 = 13 ; 10 éléments au rang 28.
- `tutorial15.sav` (*See a working city*) : 629 bâtiments, rangs 40 et 68 = 629 ; 297 éléments
  au rang 28. `map.md` relevait déjà un maximum de 629 dans la grille u16 du rang 16.
- Route de 9 cases tracée au début de la première aventure de Poséidon : bit `0x40` du rang 17
  sur les 9 cases ; 5 bâtiments créés (rang 83) puis retirés (rang 68 = 0).
- Rang 4 : la barre du haut, capturée juste avant chaque sauvegarde d'essai, affiche le trésor et
  la population ; +0x0C et +0x24 valent 10 000 et 0 (ville vide), 14 757 et 88 (didacticiel),
  9 907 et 0 (route), 25 930 et 2 220 (grande ville). La valeur 88 se retrouve aussi à +0x34,
  +0x44, +0x268C, +0x2C28, +0x2C58 et +0x4694, qui divergent dans la grande ville.
- Les sauvegardes d'essai sont reproductibles : deux exécutions de `savefile.py samples` donnent
  des fichiers identiques à l'octet.

## Fichiers testés

- `original/*/Adventures/tutorial*.sav` : 48 fichiers (16 distincts), lus en entier ;
- 8 sauvegardes d'essai de l'exe GOG, faites par `tools/savefile.py samples` (non publiées) :
  ville vide, premier didacticiel à l'ouverture et quatre minutes plus tard, route tracée,
  grande ville du dernier didacticiel, plus les sauvegardes automatiques que le jeu écrit en
  chemin (`autosave_replay.sav`, `autosave_history.sav`) ; une sauvegarde faite à la main.

`python3 tools/savefile.py check original build/essais` lit et réécrit à l'octet près les
219 fichiers présents le 2026-10-15 (48 livrés ; 171 de l'exe GOG : les sauvegardes d'essai et
celles que le jeu écrit pendant le banc de scénarios), versions 235, 238, 241 et 321, en 22 minutes
sur 8 cœurs (Python pur ; environ 6 s pour une sauvegarde).

## À faire

Champs des bâtiments (rang 35) et des personnages (rang 28) au-delà de la création et de l'état de la ville (rang 4) ;
rôle des grilles inconnues ; tables moyennes (rangs 10, 13, 31, 54, 67, 81, 101, 134).
