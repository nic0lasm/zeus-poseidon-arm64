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

## Ce qui manque pour reconstituer l'installation finale (2.1)

Les mises à jour Wise livrent certains fichiers sous forme de **correctif binaire** (flux
commençant par `01`), à appliquer sur le fichier déjà installé. Sont concernés :

- 1.1 : `Zeus.exe`, `Zeus_MM.eng`, `Zeus_Text.eng`, 4 `.pak`, `Zeus_Interface.555/.sg3`, `Zeus eventmsg.txt`
- 2.1 : `Zeus.exe` (2 768 159 o de correctif), `Zeus_MM.eng`, `sierra.inf`, `Zeus event message categories.txt`

Format du correctif, partiellement décodé :

| Offset | Taille | Champ | Statut |
|---|---|---|---|
| 0x00 | 1 | type = 1 | vérifié |
| 0x01 | 2+2 | date et heure DOS du nouveau fichier | hypothèse |
| 0x05 | 4 | CRC-32 du nouveau fichier | hypothèse |
| 0x09 | 4 | taille du nouveau fichier | hypothèse |
| 0x0D | 4 | 0x19 (constant) | inconnu |
| 0x11 | 4 | CRC-32 de l'ancien fichier | vérifié sur 3 fichiers |
| 0x15 | 4 | taille de l'ancien fichier | vérifié sur 3 fichiers |
| 0x19 | 4 | longueur N du flux de commandes | vérifié sur 3 fichiers |
| 0x1D | N | commandes (copie depuis l'ancien / insertion) | codage non décodé ; `0x80+n` = n octets littéraux (hypothèse) |
| 0x1D+N | … | octets littéraux | vérifié |

## Exécutables

| | Zeus 1.0 (`cab-zeus/Zeus.exe`) | Poséidon 2.0 (`cab-poseidon/Zeus.exe`) |
|---|---|---|
| SHA-256 | 1a800a59…aabaf3745 | 03bc3372…2290d33c4c |
| Taille | 2 971 779 | 2 857 091 |
| Compilé le | 2000-10-22 | 2001-07-30 |
| Outil | MSVC 6 (linker 6.0), i386 | idem |
| `.text` | 2 195 456 o | 1 929 216 o |
| `.data` virtuelle | 15,2 Mo | 16,9 Mo (état du jeu en variables globales) |

Imports : `DDRAW` (DirectDrawCreate seul), `MSS32` (Miles Sound System, 50 fonctions),
`BINKW32` (vidéo Bink, 14), `IJL10` (JPEG Intel), `USER32`, `GDI32`, `KERNEL32`, `WINMM`
(`timeGetTime`). Aucun import réseau.

**Protection : SecuROM 4** (4.10.00 sur 1.0, 4.52.00 sur 2.0). Preuves : sections `.cms_t`
(entropie 7,3) et `.cms_d`, point d'entrée dans `.cms_t`, bloc `AddD` + numéro de version après
le PE. Le code du jeu (`.text`) n'est pas chiffré dans son ensemble (entropie 5,7 à 7,3 par
bloc de 64 Ko, dizaines de milliers de `call`/`ret`), mais aucun appel `FF 15` vers la table
d'import n'y figure : le lien entre code et imports passe par la protection (hypothèse).
On ne contourne pas SecuROM.

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
