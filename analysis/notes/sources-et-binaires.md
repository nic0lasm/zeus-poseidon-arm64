# Sources et binaires — état au 2026-10-13

Tout ce qui suit est **vérifié** par les outils de `tools/`, sauf mention « hypothèse ».

## Sources fournies (`original/archive/`)

| Fichier | Taille | CRC-32 | Nature |
|---|---|---|---|
| `cd-zeus.iso` | 602 831 712 | 690a0406 | image CD brute, 2352 o/secteur, Mode 2, volume `ZEUS` |
| `cd-poseidon.iso` | 750 036 336 | c6ad47c1 | idem, volume `POSEIDON` |
| `mise_a_jour_1_1_fr_zeus.exe` | 12 567 377 | 2953786b | installateur Wise, 70 flux |
| `mise_a_jour_2_1_fr_poseidon.exe` | 1 248 742 | 4976c11f | installateur Wise, 19 flux |

Les 9 fichiers du téléchargement correspondent aux tailles et CRC-32 du répertoire central du zip.

## Chaîne d'extraction

| Étape | Outil | Sortie | Contrôle |
|---|---|---|---|
| image CD → fichiers | `cdimage.py` | `original/cd-zeus` (1053), `original/cd-poseidon` (1290) | tailles ISO 9660 |
| InstallShield 5, `DATA1.CAB` de Zeus | `ishield.py` | `original/cab-zeus` (807 fichiers) | décompression sans erreur (pas de MD5 dans ce format) |
| InstallShield 6, `data1.hdr`+`data1/2.cab` de Poséidon | `ishield.py` | `original/cab-poseidon` (370 fichiers) | MD5 de chaque fichier |
| installateurs Wise | `wise.py` | `original/patch-zeus-1.1`, `original/patch-poseidon-2.1` | CRC-32 de chaque flux |

Dans l'archive Poséidon, 1293 entrées n'ont pas de données : ce sont les fichiers copiés tels
quels depuis le CD (`Audio/`, `Binks/`…).

## Correctifs binaires Wise (format décodé)

Les mises à jour livrent certains fichiers sous forme de correctif à appliquer au fichier déjà
installé (10 fichiers en 1.1, 4 en 2.1, dont `Zeus.exe`). Format établi en décompilant la
bibliothèque de correctifs de l'installateur (flux n°3, fonctions `0x1000124b` et `0x100017a1`)
et **vérifié** : les 14 correctifs produisent un fichier dont la taille et le CRC-32 sont ceux
annoncés. Implémentation : `tools/wise.py` (`apply_patch`).

| Offset | Taille | Champ |
|---|---|---|
| 0x00 | 1 | nombre N d'anciennes versions acceptées |
| 0x01 | 2+2 | date et heure DOS à donner au nouveau fichier |
| 0x05 | 4+4 | CRC-32 et taille du nouveau fichier |
| 0x0D | 12×N | par ancienne version : offset de son flux, CRC-32, taille (offset 0 = fichier complet stocké après la table) |

Un flux = `u32` longueur des commandes, les commandes, puis les octets littéraux. Chaque commande
donne un nombre de littéraux à insérer puis une copie depuis l'ancien fichier :

- octet < 0x80 : 4 bits hauts = littéraux, 4 bits bas = longueur de copie ;
- sinon : littéraux sur 6 bits (prolongés si bit 6), puis un octet de copie (6 bits, prolongé si
  bit 7 ; bit 6 à 0 = un déplacement relatif signé suit) ;
- la longueur de copie est toujours augmentée de 3 ; insérer des littéraux avance aussi la
  position de lecture dans l'ancien fichier.

## Installation finale reconstituée

`tools/build_install.py` rejoue les quatre installateurs dans l'ordre et produit
`original/install/` : 1124 fichiers, 549,8 Mo. Le détail est dans `original/install-journal.tsv`.
Les musiques, voix et vidéos restent sur les CD (`original/cd-*/Audio`, `Binks`) : hypothèse, le
jeu les lit depuis le CD ou les copie selon le type d'installation, à confirmer dans le code.

## Exécutables

| | Zeus 1.0 | Poséidon 2.0 | **Poséidon 2.1 (référence)** |
|---|---|---|---|
| Fichier | `cab-zeus/Zeus.exe` | `cab-poseidon/Zeus.exe` | `install/Zeus.exe` |
| SHA-256 | 1a800a59…aabaf3745 | 03bc3372…2290d33c4c | acb0dcbc…1405b0d02f |
| Taille | 2 971 779 | 2 857 091 | 3 512 483 |
| Compilé le | 2000-10-22 | 2001-07-30 | 2001-10-31 |
| `.text` | 2 195 456 o | 1 929 216 o | 2 560 000 o |
| prologues `push ebp; mov ebp,esp` | 7143 | 115 | 8093 |
| SecuROM | 4.10.00 | 4.52.00 | présent (`.cms_t`, `AddD`) |
| case du répartiteur d'API | `0x014AE2F0` | `0x01624300` | `0x016C02F8` |
| sites `call [case]` | 523 | 370 | 489 |

Tous : MSVC 6 (linker 6.0), i386, `.data` virtuelle de 15 à 17 Mo (état du jeu en variables
globales). La 2.1 contient `inflate 1.0.7` (zlib) et la chaîne `Version 1.11`. La 2.0 est
compilée sans pointeur de pile (code plus dense), la 2.1 avec : elle se décompile mieux.

Imports : `DDRAW` (DirectDrawCreate seul), `MSS32` (Miles Sound System, 50 fonctions),
`BINKW32` (vidéo Bink, 14), `IJL10` (JPEG Intel), `USER32`, `GDI32`, `KERNEL32`, `WINMM`
(`timeGetTime`). Aucun import réseau.

**Protection : SecuROM 4** (4.10.00 sur 1.0, 4.52.00 sur 2.0). Preuves : sections `.cms_t`
(entropie 7,3) et `.cms_d`, point d'entrée dans `.cms_t`, bloc `AddD` + numéro de version après
le PE. Le code du jeu (`.text`) est en clair : dans la 2.0, 25 065 `call rel32` retombent dans
`.text`, et 25 000 accès absolus visent `.data`.

Seuls les appels aux API Windows sont masqués : aucun `call [IAT]` dans `.text` ; à la place,
tous les appels externes passent par **une seule case** de `.cms_d` (contenu statique
`0xAAAAAAAA`, remplie au lancement par SecuROM) :

Le répartiteur choisit l'API d'après l'adresse de retour. Décision (2026-10-13) : on garde
l'exécutable du CD comme référence et on **ne contourne pas SecuROM**. L'API de chaque site est
identifiée par son contexte (nombre d'arguments dépilés, constantes, usage du résultat) parmi
les 201 imports déclarés, et consignée dans `analysis/notes/`.

## Données du jeu à décoder (hors démos et bandes-annonces)

| Format | Nombre (Zeus + Poséidon) | Rôle présumé |
|---|---|---|
| `.sg3` + `.555` | 91 + 104 | index et pixels des sprites (≈ 380 Mo) |
| `.wav` | 701 | bruitages |
| `.mp3` | 679 + 927 sur les CD | musiques et voix |
| `.txt` (dossier `Model/`) | ≈ 90 | paramètres de simulation en clair |
| `.pak`, `.map`, `.set`, `.sav` | 45, 16, 8, 16 | aventures, cartes, sauvegardes |
| `.eng` | 5 | textes de l'interface |
| `.jpg`, `.bmp` | 38, 11 | écrans fixes |
| `.bik` | 2 par CD | vidéos Bink |

## Ghidra

Ghidra 12.1.4, lancé par `tools/ghidra/headless.sh` (réglages et cache confinés dans
`analysis/ghidra/`). Projet `zeus` : `Zeus.exe` 2.0 à la racine (4902 fonctions, 1,45 Mo de code
couvert sur 1,93), `Zeus.exe` 2.1 dans le dossier `2.1` (8397 fonctions dans `.text`, 2,41 Mo
couverts sur 2,56, soit 94 %). Projet `wise` : bibliothèque de correctifs de l'installateur.

Dans la 2.1, les 18 noms attribués automatiquement par Ghidra (`CSingleDocTemplate`,
`CMultiPageDHtmlDialog`…) sont de fausses reconnaissances de bibliothèque : à ignorer.
Fonction la plus appelée : `0x0040DF00` (648 appelants), à identifier en priorité.

## Édition GOG (reçue le 2026-10-13)

`original/archive/setup_zeus_and_poseidon_2.1.0.10.exe` : installateur Inno Setup de GOG
(SHA-256 `f6f31391…`, identifiant de jeu GOG 1207659039), version anglaise 2.1. Extrait par
`innoextract -d original/gog` (781 Mo) ; le jeu est dans `original/gog/app/` (`Adventures`,
`Audio`, `Binks`, `DATA`, `Model`, `Save`, `Zeus.ini`, bibliothèques).

`original/gog/app/Zeus.exe` : 2 527 232 octets, SHA-256
`382bcd286192a06eb8fdc45f5c37390a19999f5db9c940c3ce2dfcd775e90a57`, compilé le 2002-01-04,
Visual C++ 6. **Aucune section SecuROM**, point d'entrée `0x005CD620` dans `.text`, 408 appels
directs à travers la table d'imports : vérifié par `tools/pe_triage.py`. C'est le seul exécutable
du jeu que le projet exécute, et seulement dans le processeur émulé de `src/win32/`.

Projet Ghidra `zeus`, dossier `gog` (analyse automatique complète, 133 s). La procédure de
fenêtre n'y est pas reconnue comme fonction (elle n'est désignée que par `RegisterClassA`) :
la lire par ses tables de sauts (`docs/reverse-engineering.md`).
