# Zeus + Poséidon sur Mac Apple Silicon

[![Dernière version](https://img.shields.io/github/v/release/nic0lasm/zeus-poseidon-arm64?label=t%C3%A9l%C3%A9charger&color=1f6feb)](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest)
[![macOS 14+ Apple Silicon](https://img.shields.io/badge/macOS%2014%2B-Apple%20Silicon-333?logo=apple)](#télécharger)
[![build](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml/badge.svg)](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml)
[![Prouvé identique : 10 scénarios](https://img.shields.io/badge/prouv%C3%A9%20identique-10%20sc%C3%A9narios-2ea043)](docs/equivalence.md)
[![Licence : GPL-2.0-or-later](https://img.shields.io/badge/licence-GPL--2.0--or--later-green)](LICENSE)

**Jouer à *Zeus : Le Maître de l'Olympe* et à son extension *Poséidon* sur un Mac Apple
Silicon : le jeu d'origine, sans Windows, sans Wine et sans machine virtuelle.**

*[English version](README.md)*

<p align="center">
  <img src="docs/images/city-wide-fr.jpg" width="100%" alt="Une grande ville en mode large (1324 × 768), avec le pack français">
</p>
<p align="center">
  <img src="docs/images/menu-fr.png" width="49%" alt="Le menu principal avec le pack français">
  <img src="docs/images/tutorials.png" width="49%" alt="L'écran des tutoriels">
</p>
<p align="center"><sub>Une grande ville en mode large, et le menu principal en français, à partir de votre propre exemplaire du jeu.</sub></p>

## En un coup d'œil

| | |
|---|---|
| **Ce que c'est** | Le vrai *Zeus* + *Poséidon* (Impressions Games, 2000) : le code d'origine du jeu, non modifié, exécuté sur un Mac. Même simulation, mêmes règles, mêmes sauvegardes. Pas une réécriture. |
| **État** | Jouable du début à la fin. Dernière version : **[1.6](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest)** (17 octobre 2026). |
| **Fonctionne sur** | Mac à puce Apple Silicon (M1 ou plus récent), macOS 14 ou plus récent ; Mac à processeur Intel, macOS 15 ou plus récent ; Linux x86-64 et arm64. Voir [Télécharger](#télécharger). |
| **Il vous faut** | Votre propre copie du jeu, édition GOG (ou Steam, [voir plus bas](#acheté-sur-steam-)). Ce dépôt ne contient aucun fichier du jeu ([Mentions légales](#mentions-légales)). |
| **Fidèle** | Dix scénarios rejoués comparent chaque image et tout l'état de la partie, tour par tour, avec le jeu d'origine. |
| **Léger** | Une petite ville occupe environ 23 % d'un cœur (51 à 60 % en 1.0.1). [Chiffres](#plus-léger-à-chaque-version). |

## Le principe en bref

*Zeus* est un jeu Windows 32 bits que les Mac récents ne savent plus lancer. Ce projet exécute
son **code d'origine** dans un petit émulateur x86 cloisonné, et réécrit en natif les fonctions
Windows dont il a besoin : fichiers, fenêtre, souris, clavier, DirectDraw, son et vidéo. Les
parties les plus sollicitées du jeu (l'essentiel du dessin de la ville, les parties les plus
coûteuses de la simulation) sont aussi réécrites à la main en natif, chacune prouvée identique à
l'original. [Détails plus bas](#principe).

## Télécharger

| Votre ordinateur | Téléchargement | |
|---|---|---|
| Mac à puce Apple Silicon (M1 ou plus récent), macOS 14+ | **[⬇ Zeus-Poseidon-macos-arm64.zip](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest/download/Zeus-Poseidon-macos-arm64.zip)** | disponible |
| Mac à processeur Intel, macOS 15+ | **[⬇ Zeus-Poseidon-macos-intel.zip](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest/download/Zeus-Poseidon-macos-intel.zip)** | disponible |
| Linux x86-64 (Ubuntu 22.04, Debian 12, Fedora 36 ou plus récents) | **[⬇ Zeus-Poseidon-linux-x86_64.AppImage](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest/download/Zeus-Poseidon-linux-x86_64.AppImage)** | disponible |
| Linux arm64 (mêmes distributions) | **[⬇ Zeus-Poseidon-linux-aarch64.AppImage](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest/download/Zeus-Poseidon-linux-aarch64.AppImage)** | disponible |

Toutes les versions et leurs notes : [Releases](https://github.com/nic0lasm/zeus-poseidon-arm64/releases).

### Installer et jouer

1. Ouvrez le zip téléchargé (double-clic), puis double-cliquez sur **Zeus Poseidon**.
2. macOS refuse de l'ouvrir la première fois, car l'application n'est pas notariée par Apple.
   Cliquez sur **OK**, puis ouvrez **menu Pomme → Réglages Système → Confidentialité et
   sécurité**, descendez jusqu'au message sur « Zeus Poseidon » et cliquez sur **Ouvrir quand
   même** ; confirmez avec votre mot de passe ou Touch ID, puis cliquez sur **Ouvrir**. Une seule
   fois. *(Les libellés peuvent varier légèrement selon la version de macOS.)*
3. Désignez votre installateur GOG (`setup_zeus_and_poseidon_….exe` ; le sélecteur s'ouvre dans
   Téléchargements) ou déposez-le sur l'icône de l'application. Il est déballé une fois pour
   toutes (environ une minute, 800 Mo) ; ensuite, jouez. Pour l'obtenir depuis un Mac :
   connectez-vous sur **GOG.com → bibliothèque → Zeus + Poseidon → installateurs Windows hors
   ligne (« Télécharger les installateurs hors ligne ») → Windows** (téléchargez le `.exe` et les
   fichiers `.bin` qui l'accompagnent, s'il y en a).

**En français ?** Gardez la touche Option (⌥) enfoncée au lancement et choisissez *Autre langue,
depuis les CD d'origine…* : l'application fabrique un pack français à partir des images de vos
propres CD ([détails](docs/languages.md), en anglais).

**Sous Linux :** rendez l'AppImage exécutable (`chmod +x`, ou *Propriétés → Autoriser
l'exécution*) et lancez-la. Elle demande l'installateur GOG de la même façon (avec zenity ou
kdialog s'ils sont installés, sinon dans le terminal) ; lancez-la avec `ZEUS_ASK_LANGUAGE=1` pour
choisir la langue.

### Acheté sur Steam ?

D'après SteamDB, l'édition Steam contient les mêmes fichiers que celle de GOG (mêmes noms et
tailles, `Zeus.exe` compris, et aucune bibliothèque Steam), mais personne ne l'a encore essayée
([#128](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/128)).
- **Sous Linux**, installez-la avec Steam (Proton) : l'application la trouve seule dans vos
  bibliothèques Steam, rien à désigner.
- **Sur Mac**, Steam ne sait pas installer ce jeu réservé à Windows : copiez le dossier
  `Zeus + Poseidon` depuis un PC Windows (`Steam\steamapps\common`), puis choisissez-le avec
  **Dossier…**.

Si son `Zeus.exe` n'est pas celui de GOG, l'application le signale une fois et affiche son
empreinte : merci de la signaler sur [#128](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/128).

## Ce que vous obtenez

- **Tout le jeu :** les seize tutoriels, les aventures de Zeus et de Poséidon, l'éditeur
  d'aventures, dieux, héros, monstres et catastrophes, sauvegarde et chargement.
- **Le son et l'image :** musiques, ambiances, son 3D, bruitages et vidéo d'introduction.
- **Une fenêtre moderne :** fenêtre ou plein écran (⌘F), nette à toute taille, et un mode large
  adapté à votre écran (prenez la plus grande résolution dans les options d'affichage du jeu).
- **Votre langue :** l'anglais d'emblée, le français (et d'autres éditions, voir
  [#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)) depuis vos propres CD.
- **Le confort :** molette dans les listes, pause quand vous passez à une autre application,
  démarrage en moins d'une seconde.
- **La fidélité prouvée :** dix scénarios rejoués comparent chaque image et tout l'état de la
  partie, tour par tour, avec le jeu d'origine : une modification qui changerait un seul pixel
  ou un seul nombre de la simulation ne peut pas passer inaperçue
  ([equivalence.md](docs/equivalence.md), en anglais).

## Plus léger à chaque version

Chaque version fait passer davantage de travail de l'original émulé à du code natif écrit à la
main, chaque partie prouvée identique à l'original.

| Mesure | 1.0.1 | 1.1 | 1.2 | 1.3 | 1.4 |
|---|---:|---:|---:|---:|---:|
| Petite ville, temps réel sur Mac (part d'un cœur) | 51–60 % | environ 35 % | environ 23 % | pas encore mesurée ([#127](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/127), avis aux possesseurs de Mac) | |
| Tour de simulation d'une grande ville (banc, `working-city`) | | 5,3–5,8 ms | 1,6 ms | 1,6 ms | **1,6 ms** |
| Dessin de la ville resté en natif (second passage, grande ville) | | | 97 % des cases | 97 % des cases | **97 %** des cases |
| Jeu figé par une sauvegarde (grande ville, temps réel) | | | 3,85 s | 0,33 s | **0,33 s** |
| Conversion des couleurs d'une image 1024×768 pour la fenêtre | | | | 4,1 ms | **0,65 ms** |

Aussi dans la 1.2 : les cadres des fenêtres en natif (10 à 12 % de calcul en moins sur les
scénarios de la grande ville et des options). Nouveau dans la 1.3 : les
textes d'une ligne en natif, 7 à 9 % de calcul en moins en jeu réel sur la grande ville
([#113](https://github.com/nic0lasm/zeus-poseidon-arm64/pull/113)). Nouveau dans la 1.4 : un affichage plus léger, 7 à 12 % de calcul
en moins fenêtre visible ([#131](https://github.com/nic0lasm/zeus-poseidon-arm64/pull/131)).

**Avancée du moteur :** 322 des 4 872 fonctions de l'exécutable GOG sont comprises (nommées,
documentées ou natives) et 24 sont remplacées entières par du code natif ; le code natif fait
53,2 % du temps de calcul du jeu sur `working-city`, plus 17,1 % dans des fonctions natives en
partie. [Carte du code et compteurs](docs/engine/README.md) (en anglais).

## La suite

Les gains de vitesse qu'un joueur pouvait sentir dans le code émulé sont faits ; chaque morceau
qui reste pèse environ 1 % du temps ou moins. Les prochaines étapes visent ce que les joueurs
remarquent ([plan complet](docs/roadmap.md#after-13-review-of-16-october-2026-evening) en anglais, [jalon 1.4](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/12)) :

Fait dans la 1.4 : **un affichage plus léger** ([#125](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/125)) ; **le chargement d'une
partie** mesuré à 0,16 s, rien à accélérer ([#126](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/126)). Ensuite :

1. **D'autres éditions** : les packs de langue ([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)) et l'édition Steam ([#128](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/128)). Si vous en possédez une, votre retour aide plus que toute nouveauté.

À plus long terme, le but ne change pas : que *Zeus* et *Poséidon* restent jouables aussi
longtemps qu'on aura envie d'y jouer. Un moteur entièrement natif (2.0, sans émulateur, comme
Julius l'a fait pour *Caesar III*) est en pause ; la carte du code, les fiches de
[`docs/engine/`](docs/engine/) et le banc restent prêts. Aucune date n'est promise.

## Historique des versions

| Version | Points forts |
|---|---|
| [**1.6**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.6) | les applications Mac déballent de nouveau seules l'installateur GOG (innoextract embarqué, [#134](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/134), merci à [@MohammadBnei](https://github.com/MohammadBnei)) |
| [**1.5**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.5) | édition Steam trouvée seule (bibliothèques Linux, Flatpak, Mac) ; `Zeus.exe` contrôlé au lancement, avec un avertissement pour une version inconnue |
| [**1.4**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.4) | affichage plus léger : images copiées sur place et texture de la fenêtre au format du rendu (7 à 12 % de calcul en moins fenêtre visible ; conversion des couleurs 4,1 → 0,65 ms par image) |
| [**1.3**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.3) | sauvegardes sans pause (3,85 s → 0,33 s sur une grande ville) ; archive pour Mac Intel (macOS 15+) ; AppImage Linux (x86-64, arm64) ; textes d'une ligne en natif (7 à 9 % de calcul en moins) |
| [**1.2**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.2) | plus léger : dessin de la ville, cadres des fenêtres et textes en natif (petite ville à environ 23 % d'un cœur au lieu de 35 %) ; tour de simulation d'une grande ville environ trois fois plus rapide ; dix scénarios prouvés identiques à l'original, tour par tour |
| [**1.1**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.1) | premier lancement en moins de clics (téléchargement en un clic, sélecteur dans Téléchargements, plus de question de langue) ; modes larges plus hauts ; sol de la ville dessiné en natif (petite ville à 35 % d'un cœur au lieu d'environ 50 %) |
| [**1.0.1**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0.1) | le jeu à sa vraie vitesse (il tournait aux deux tiers depuis la 0.2) ; répartiteur de sprites natif |
| [**1.0**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0) | première version stable : le code de la 0.9, joué à la main |

<details>
<summary>Versions plus anciennes (0.1 à 0.9)</summary>

| Version | Points forts |
|---|---|
| [0.9](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.9) | version candidate : démarrage en moins d'une seconde, vraie résolution affichée en mode large, moins de mémoire |
| [0.4](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.4) | son 3D, routines de dessin natives, molette, pause en arrière-plan, essais automatiques bien plus larges |
| [0.3](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.3) | écran large ; packs de langue fabriqués par l'application depuis les CD d'origine |
| [0.2](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.2) | le français ; deux fois moins de processeur ; l'application ouvre elle-même l'installateur GOG |
| [0.1](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.1) | premier aperçu : menus, tutoriels, construction, sauvegarde et chargement, musiques, bruitages, vidéo d'introduction, plein écran |

</details>

Les archives des versions 0.1 et 0.2 ont été retirées (elles contenaient des chemins de la
machine qui les avait fabriquées) ; chaque changement est décrit dans [CHANGELOG.md](CHANGELOG.md)
(en anglais).

## Compiler depuis les sources

Il faut : un Mac Apple Silicon, les outils en ligne de commande Xcode,
[Homebrew](https://brew.sh) et l'édition GOG de *Zeus + Poseidon*.

```bash
brew install cmake ninja pkgconf sdl2 unicorn ffmpeg innoextract
git clone https://github.com/nic0lasm/zeus-poseidon-arm64.git
cd zeus-poseidon-arm64

# Déballer son propre installateur GOG (le jeu n'est pas installé, seulement déballé)
innoextract -d original/gog /chemin/vers/setup_zeus_and_poseidon_2.1.0.10.exe

cmake -S . -B build -G Ninja && cmake --build build
./build/zeus-run --run original/gog/app/Zeus.exe --wide auto
```

`python3 tools/make_app.py` fabrique une application `Zeus Poseidon.app` à double-cliquer. Le
dossier du jeu n'est jamais modifié : tout ce que le jeu écrit (sauvegardes, réglages) va dans
`run/zeus/`. Options, parcours scriptés et dépannage :
[docs/getting-started.md](docs/getting-started.md).

## Principe

Le jeu importe 202 fonctions Windows. Chacune est une petite fonction native dans `src/win32/` ;
une fonction pas encore écrite arrête le programme en se nommant, ce qui a guidé le travail. Les
routines les plus sollicitées du jeu (dessin des sprites et leur répartiteur, l'essentiel du
dessin de la ville, cadres des fenêtres et textes, et les parties les plus coûteuses de la
simulation : recherches de chemin, marcheurs, bâtiments composés) tournent aussi en natif,
chacune prouvée identique image par image et tour par tour. Le jeu voit un
dossier `C:\Zeus` privé : les lectures aboutissent dans vos fichiers du jeu, les écritures dans
un dossier séparé. Voir [docs/architecture.md](docs/architecture.md).

## Également dans ce dépôt

Le projet a commencé par l'étude des données et du code du jeu ; ce travail est documenté pour
quiconque fabrique des outils pour la communauté *Zeus* / *Poséidon* :

- **Formats de fichiers :** [`formats/`](formats/) décrit les sprites (`.sg3` + `.555`), les
  cartes, les sauvegardes (avec leur compression), les aventures, les textes et l'aide du jeu,
  chacun avec un lecteur de référence en Python ; d'autres à venir ([#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30)).
- **Outils d'extraction :** lecteurs en pur Python des images CD d'origine, des archives
  InstallShield et des installateurs de mises à jour Wise ([`tools/`](tools/)).
- **Visionneuses :** `zeus-sprites` (tous les sprites du jeu) et `zeus-map` (terrain
  isométrique d'une carte).
- **Notes de rétro-ingénierie :** imports, symboles, comportement du moteur
  ([`analysis/notes/`](analysis/notes/), [docs/reverse-engineering.md](docs/reverse-engineering.md)).

## Communauté

C'est un projet de joueurs, pour les joueurs. Venez dire comment le jeu tourne sur votre Mac,
poser vos questions ou proposer des idées dans les
[Discussions](https://github.com/nic0lasm/zeus-poseidon-arm64/discussions) ; les bugs vont dans
les [tickets](https://github.com/nic0lasm/zeus-poseidon-arm64/issues).

L'aide la plus utile aujourd'hui :

- **Jouer et raconter :** une aventure jouée jusqu'au bout, sur votre modèle de Mac.
- **Une autre édition linguistique ?** Essayez un pack depuis vos CD et dites-nous
  ([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)).
- **Du code :** voir les [tickets ouverts](https://github.com/nic0lasm/zeus-poseidon-arm64/issues) et
  [CONTRIBUTING.md](CONTRIBUTING.md).

### Merci

Les retours des joueurs font avancer ce projet. Merci à :

- [**@MohammadBnei**](https://github.com/MohammadBnei), qui a découvert sur un Mac neuf que
  l'application ne savait pas déballer l'installateur GOG, en a trouvé la cause (innoextract
  manquant) et proposé la correction livrée dans la 1.6 ([#134](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/134)).

Votre nom peut être le suivant : chaque retour, essai ou pull request compte.

## Mentions légales

Projet de fans indépendant et non commercial, sans lien avec Impressions Games, Sierra,
Activision ou GOG, qui ne le soutiennent ni ne l'approuvent. *Zeus : Le Maître de l'Olympe* et
*Poséidon* sont des marques de leurs propriétaires respectifs.

Le dépôt ne contient que du code source original, des outils et de la documentation. Il ne
distribue aucun exécutable, image, son, texte ou donnée du jeu, et ne contourne aucune
protection : il n'exécute que l'édition GOG, sans DRM, à partir des fichiers que vous
fournissez.

## Licence

GPL-2.0-or-later, voir [LICENSE](LICENSE). Elle ne couvre que le code et la documentation de
ce dépôt, jamais le jeu.

L'émulation x86 repose sur [Unicorn](https://www.unicorn-engine.org/) (GPL-2.0) ; les vidéos
sont décodées par [FFmpeg](https://ffmpeg.org/). Voir [docs/third-party.md](docs/third-party.md).
