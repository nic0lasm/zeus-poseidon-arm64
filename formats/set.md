# `.set` — aventure en cours d'édition

Un `.set` est l'état complet d'une aventure dans l'éditeur : textes et scénarios de chaque
épisode, événements, carte de la ville mère. C'est aussi l'en-tête de chaque `.pak`
([`pak.md`](pak.md)). Adresses de l'exécutable GOG ; tout est little-endian.

Outil : `tools/setfile.py` (`info`, `check`). État au 2026-10-15 : les 28 `.set` des
installations (11 distincts, versions 5 et 26) se lisent jusqu'au dernier octet et se
réécrivent à l'identique.

## Routines

| Adresse | Rôle | Statut |
|---|---|---|
| `0x00425910` | écriture : la constante `0x22A7`, puis 54 champs de l'objet aventure par `fwrite`, sans alignement | vérifié |
| `0x00426880` | sauvegarde de l'aventure dans l'éditeur : met la version à 26, ouvre `Adventures\NOM.set` (`0x004263B0`), appelle `0x00425910` puis `0x0052DEF0` (4 variables globales) | vérifié |
| `0x00425D00` | lecture ; champs ignorés ou adaptés selon la version (ci-dessous) | vérifié |
| `0x0052DF50` | lecture de la suite (4 variables globales) | vérifié |
| `0x00426340` | lit seulement la version puis l'octet `kind` (décalage `0x49530`, ou `0x49108` avant la version 23) | vérifié |

## Versions

`0x00425D00` lit la version ; à 1 ou moins, il relit tout le fichier comme l'objet de
`0xCB28C` octets d'un bloc (aucun fichier livré). Sinon, champ par champ :

- avant la version 23, `episode_extra` et `colony_extra` ont 224 octets par élément au lieu de
  300 (comme le rang 122 des sauvegardes de version 235) ;
- `value_J` existe à partir de la version 3, `table_K` et `table_L` de la 4, `table_M` et
  `value_N` de la 5, `value_O` de la 21 ;
- corrections après lecture : avant la version 22, appel de `0x00425260` ; avant la 24, `value_O`
  est recopié dans un champ de chacun des 14 scénarios ; avant la 25, les événements dont un
  octet vaut 7 sont réécrits (type 0x18) ; avant la 26, un octet des événements de type 4 est
  remis à zéro.

En-tête : 842 931 octets en version 26, 841 863 en version 5 (vérifié sur tous les fichiers).

## Champs

Décalage dans le fichier (versions 26 et 5), taille, décalage dans l'objet aventure (ou adresse
d'une variable globale), nom provisoire, contenu.

| Fichier v26 | Fichier v5 | Taille | Objet | Nom | Contenu | Statut |
|---|---|---|---|---|---|---|
| `0x0` | `0x0` | 4 | — | `magic` | 0x22A7 | vérifié |
| `0x4` | `0x4` | 4 | `+0x4` | `version` | version du format : 5 ou 26 dans les fichiers livrés | vérifié |
| `0x8` | `0x8` | 4 | `+0x8` | `episodes` | nombre d'épisodes de la ville mère (hypothèse) | hypothèse |
| `0xc` | `0xc` | 4 | `+0xc` | `value_b` | entier | inconnu |
| `0x10` | `0x10` | 4 | `+0x10` | `colonies` | nombre de colonies (hypothèse) | hypothèse |
| `0x14` | `0x14` | 3560 | `+0x14` | `episode_texts` | 10 × 356 octets : présentation des épisodes de la ville mère | hypothèse |
| `0xdfc` | `0xdfc` | 3560 | `+0xdfc` | `colony_texts` | 10 × 356 octets, 4 utilisés : épisodes de colonie | hypothèse |
| `0x1be4` | `0x1be4` | 20320 | `+0x1be4` | `episode_scenarios` | 10 × 2 032 octets : description de scénario par épisode (rang 9 des sauvegardes) | hypothèse |
| `0x6b44` | `0x6b44` | 8128 | `+0x6b44` | `colony_scenarios` | 4 × 2 032 octets : même chose pour les colonies | hypothèse |
| `0x8b04` | `0x8b04` | 4 | `+0x8b04` | `value_h` | entier | inconnu |
| `0x8b08` | `0x8b08` | 4 | `+0x8b08` | `value_i` | entier | inconnu |
| `0x8b0c` | `0x8b0c` | 16 | `+0x8b0c` | `table_j` | 16 octets | inconnu |
| `0x8b1c` | `0x8b1c` | 4 | `+0x8b1c` | `value_k0` | entier | inconnu |
| `0x8b20` | `0x8b20` | 4 | `+0x8b20` | `value_k1` | entier | inconnu |
| `0x8b24` | `0x8b24` | 4 | `+0x8b24` | `value_k2` | entier | inconnu |
| `0x8b28` | `0x8b28` | 4 | `+0x8b28` | `value_k3` | entier | inconnu |
| `0x8b2c` | `0x8b2c` | 4 | `+0x8b2c` | `value_k4` | entier | inconnu |
| `0x8b30` | `0x8b30` | 4 | `+0x8b30` | `value_k5` | entier | inconnu |
| `0x8b34` | `0x8b34` | 4 | `+0x8b34` | `value_k6` | entier | inconnu |
| `0x8b38` | `0x8b38` | 4 | `+0x8b38` | `value_k7` | entier | inconnu |
| `0x8b3c` | `0x8b3c` | 4 | `+0x8b3c` | `value_k8` | entier | inconnu |
| `0x8b40` | `0x8b40` | 4 | `+0x8b40` | `value_k9` | entier | inconnu |
| `0x8b44` | `0x8b44` | 4 | `+0x8b44` | `value_k10` | entier | inconnu |
| `0x8b48` | `0x8b48` | 40 | `+0x8b48` | `table_l` | 10 entiers | inconnu |
| `0x8b70` | `0x8b70` | 40 | `+0x8b70` | `table_m` | 10 entiers | inconnu |
| `0x8b98` | `0x8b98` | 3000 | `+0x8b98` | `episode_extra` | 10 × 300 octets (10 × 224 avant la version 23) : rang 122 des sauvegardes | hypothèse |
| `0x9750` | `0x9458` | 1200 | `+0x9750` | `colony_extra` | 4 × 300 octets (4 × 224 avant la version 23) | hypothèse |
| `0x9c00` | `0x97d8` | 186000 | `+0x9c00` | `episode_events` | 10 × 18 600 octets : 150 événements de 124 octets par épisode (rang 13 des sauvegardes) | hypothèse |
| `0x37290` | `0x36e68` | 74400 | `+0x37290` | `colony_events` | 4 × 18 600 octets : événements des colonies | hypothèse |
| `0x49530` | `0x49108` | 1 | `+0x49530` | `kind` | genre : 0 campagne Zeus, 1 et 4 aventure rangée dans son propre dossier, 2 didacticiel, 3 campagne Poséidon | vérifié |
| `0x49531` | `0x49109` | 4 | `+0x49534` | `value_s` | entier, 0 partout | inconnu |
| `0x49535` | `0x4910d` | 20 | `+0x49538` | `map_offsets` | 5 décalages dans le .pak : carte de la ville mère puis colonies | vérifié |
| `0x49549` | `0x49121` | 500000 | `+0x4954c` | `parent_map` | conteneur MAPS complété de zéros (carte de la ville mère, hypothèse) | vérifié (conteneur) |
| `0xc3669` | `0xc3241` | 40 | `+0xc366c` | `episode_event_counts` | 10 entiers : nombre d'événements de chaque épisode | vérifié |
| `0xc3691` | `0xc3269` | 16 | `+0xc3694` | `colony_event_counts` | 4 entiers : nombre d'événements de chaque colonie | vérifié |
| `0xc36a1` | `0xc3279` | 640 | `+0xc36a4` | `table_x` | 640 octets | inconnu |
| `0xc3921` | `0xc34f9` | 10240 | `+0xc3924` | `table_y` | 10 240 octets | inconnu |
| `0xc6121` | `0xc5cf9` | 10240 | `+0xc6124` | `table_z` | 10 240 octets | inconnu |
| `0xc8921` | `0xc84f9` | 256 | `+0xc8924` | `table_A` | 256 octets | inconnu |
| `0xc8a21` | `0xc85f9` | 4096 | `+0xc8a24` | `table_B` | 4 096 octets | inconnu |
| `0xc9a21` | `0xc95f9` | 4096 | `+0xc9a24` | `table_C` | 4 096 octets | inconnu |
| `0xcaa21` | `0xca5f9` | 64 | `+0xcaa24` | `table_D` | 64 octets | inconnu |
| `0xcaa61` | `0xca639` | 1024 | `+0xcaa64` | `table_E` | 1 024 octets | inconnu |
| `0xcae61` | `0xcaa39` | 1024 | `+0xcae64` | `table_F` | 1 024 octets | inconnu |
| `0xcb261` | `0xcae39` | 40 | `+0xcb264` | `table_G` | 10 entiers, 50 par défaut | inconnu |
| `0xcb289` | `0xcae61` | 16 | `+0xcb28c` | `table_H` | 4 entiers | inconnu |
| `0xcb299` | `0xcae71` | 4096 | `+0xcb29c` | `table_I` | 4 096 octets | inconnu |
| `0xcc299` | `0xcbe71` | 4 | `+0xcc29c` | `value_J` | entier | inconnu |
| `0xcc29d` | `0xcbe75` | 110 | `+0xcc2a0` | `table_K` | 110 octets | inconnu |
| `0xcc30b` | `0xcbee3` | 110 | `+0xcc30e` | `table_L` | 110 octets | inconnu |
| `0xcc379` | `0xcbf51` | 10 | `+0xcc37c` | `table_M` | 10 octets | inconnu |
| `0xcc383` | `0xcbf5b` | 4 | `+0xcc386` | `value_N` | entier | inconnu |
| `0xcc387` | — | 4 | `+0xcc7d8` | `value_O` | entier | inconnu |
| `0xcc38b` | `0xcbf5f` | 16 | `0x00fe3b5c` | `world_1` | 16 octets, variable globale 0x00FE3B5C | inconnu |
| `0xcc39b` | `0xcbf6f` | 1824 | `0x00fe3b6c` | `world_2` | 1 824 octets, variable globale 0x00FE3B6C | inconnu |
| `0xccabb` | `0xcc68f` | 40 | `0x00fe2964` | `world_3` | 40 octets, variable globale 0x00FE2964 | inconnu |
| `0xccae3` | `0xcc6b7` | 4560 | `0x00fe298c` | `world_4` | 4 560 octets, variable globale 0x00FE298C | inconnu |

### Preuves et observations

- `kind` : `0x0042B230` cherche le `.pak` dans `Adventures\NOM\` quand il vaut 1 ou 4, sinon
  dans `Adventures\`. Sur les 125 `.pak` d'aventure, les 24 de genre 1 ou 4 sont rangés dans un
  dossier de leur nom, aucun des autres. Genre 0 : campagnes et jeux ouverts de Zeus ; 2 : les
  16 didacticiels (`housing.pak`…) ; 3 : campagnes de Poséidon.
- `episode_event_counts` et `colony_event_counts` bornent les boucles de correction de
  `0x00425D00` sur les événements de chaque épisode (pas de 124 octets, comme le rang 13 des
  sauvegardes).
- `episode_texts` : `0x00426880` en recopie 10 fois 356 octets dans l'objet ; `colony_texts`
  4 fois, alors que le champ en a la place pour 10.
- Tailles qui coïncident avec des blocs des sauvegardes : 2 032 (rang 9), 18 600 = 150 × 124
  (rang 13), 300 / 224 (rang 122) ; d'où les hypothèses « par épisode ».
- `episodes` et `colonies` : dans les `.pak`, `colonies` vaut le nombre de cartes de colonie
  incluses pour 52 fichiers distincts sur 56 ; `episodes`, le nombre d'épisodes ayant des événements, pour la
  plupart.
- `parent_map` contient un conteneur MAPS complet (lu par `mapfile.py`) suivi de zéros.

## Fichiers testés

`original/*/Adventures/**/*.set` : 28 fichiers, dont 11 distincts.
