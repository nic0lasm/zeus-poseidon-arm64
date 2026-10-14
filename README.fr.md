# Zeus + Poséidon sur Mac Apple Silicon

[![Dernière version](https://img.shields.io/github/v/release/nic0lasm/zeus-poseidon-arm64?label=t%C3%A9l%C3%A9charger&color=1f6feb)](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest)
[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple%20Silicon-333?logo=apple)](docs/getting-started.md)
[![build](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml/badge.svg)](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml)
[![Licence : GPL-2.0-or-later](https://img.shields.io/badge/licence-GPL--2.0--or--later-green)](LICENSE)

**Jouer à *Zeus : Le Maître de l'Olympe* et à son extension *Poséidon* sur un Mac Apple
Silicon — le jeu d'origine, sans Windows, sans Wine et sans machine virtuelle.**

*[English version](README.md)*

<p align="center">
  <img src="docs/images/city-wide-fr.jpg" width="100%" alt="Une grande ville en mode large (1324 × 768), avec le pack français">
</p>
<p align="center">
  <img src="docs/images/menu-fr.png" width="49%" alt="Le menu principal avec le pack français">
  <img src="docs/images/tutorials.png" width="49%" alt="L'écran des tutoriels">
</p>
<p align="center"><sub>Une grande ville en mode large, et le menu principal en français — à partir de votre propre exemplaire du jeu.</sub></p>

*Zeus* (Impressions Games, 2000) est un jeu Windows 32 bits que les Mac récents ne savent plus
lancer. Ce projet exécute son **code d'origine, non modifié**, dans un petit émulateur x86
cloisonné, et réécrit en natif les fonctions Windows dont il a besoin : fichiers, fenêtre,
souris, clavier, DirectDraw, son et vidéo. Le résultat est le vrai jeu — même simulation, mêmes
règles, mêmes sauvegardes — et non une réécriture.

> **Ce dépôt ne contient aucun fichier du jeu.** Il faut posséder sa propre copie (édition GOG).
> Voir [Mentions légales](#mentions-légales).

## Jouer en trois étapes

1. Téléchargez **`Zeus-Poseidon-…-macos-arm64.zip`** depuis la
   [dernière version](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest) et
   décompressez-le.
2. Clic droit sur **Zeus Poseidon.app** → **Ouvrir** (la première fois seulement : l'application
   n'est pas signée par Apple).
3. Désignez votre installateur GOG (`setup_zeus_and_poseidon_….exe`). Il est déballé une fois
   pour toutes ; ensuite, jouez.

**En français ?** Gardez la touche Option (⌥) enfoncée au lancement et choisissez *Autre langue,
depuis les CD d'origine…* : l'application fabrique un pack français à partir des images de vos
propres CD ([détails](docs/languages.md), en anglais).

## Ce que vous obtenez

- **Tout le jeu** — les seize tutoriels, les aventures de Zeus et de Poséidon, l'éditeur
  d'aventures, dieux, héros, monstres et catastrophes, sauvegarde et chargement.
- **Le son et l'image** — musiques, ambiances, son 3D, bruitages et vidéo d'introduction.
- **Une fenêtre moderne** — fenêtre ou plein écran (⌘F), nette à toute taille, et un mode large
  adapté à votre écran : prenez la plus grande résolution dans les options d'affichage du jeu.
- **Votre langue** — l'anglais d'emblée, le français (et d'autres éditions, voir plus bas)
  depuis vos propres CD.
- **Le confort** — molette dans les listes, pause quand vous passez à une autre application,
  démarrage en moins d'une seconde.
- **La fidélité prouvée** — huit scénarios rejoués comparent chaque image à une référence : une
  modification qui changerait un seul pixel du jeu ne peut pas passer inaperçue.

## La suite

Le but : que *Zeus* et *Poséidon* restent jouables aussi longtemps qu'on aura envie d'y jouer — le
jeu d'origine, sur les machines qu'on possède, prouvé identique à l'original.

### Maintenant — 1.1 *Sobre et silencieux*
Le moins de processeur possible, pour un portable qui reste frais et silencieux.
[Jalon](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/1)

- [x] Répartiteur de sprites natif : 42 % de calcul en moins dans une grande ville ([#21](https://github.com/nic0lasm/zeus-poseidon-arm64/pull/21))
- [x] Le jeu à sa vraie vitesse : 45 tours de simulation par seconde au lieu de 30 ([#32](https://github.com/nic0lasm/zeus-poseidon-arm64/pull/32))
- [ ] Boucle de dessin de la ville en natif ([#34](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/34))
- [ ] Textes du panneau de contrôle en natif ([#35](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/35))
- [ ] Peut-on éviter les dessins jamais affichés ? — étude ([#36](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/36))
- [ ] Modes larges plus hauts, sans bande noire sous le panneau ([#23](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/23))

**Objectif :** une petite ville sous 20 % d'un cœur (environ 42 % aujourd'hui), la grande ville
du dernier tutoriel sous 35 %.

### Ensuite — 1.2 *Toutes les éditions*
Packs allemand, espagnol, italien, polonais, néerlandais, suédois, tchèque… fabriqués depuis vos
CD. **Testeurs recherchés** : si vous possédez une autre édition, votre retour fait toute
l'étape ([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)).

### Puis — 2.0 *Partout*
Le même hôte sous **Linux** (x86-64 et arm64) et sur les **Mac Intel**. Seules quatre petites
pièces dépendent aujourd'hui de macOS. [Jalon](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/3)

- [ ] Décodage MP3 portable ([#37](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/37)) · décodage JPEG ([#38](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/38)) · fenêtres du premier lancement ([#39](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/39))
- [ ] Compilation Linux dans la compilation automatique, puis une AppImage ([#40](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/40))
- [ ] Une application Mac universelle, Intel et Apple Silicon ([#41](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/41))

### Plus tard — 3.0 *Recompilé*
**Plus d'émulateur du tout.** Votre propre `Zeus.exe` traduit en code natif *sur votre machine* —
comme un pack de langue, rien du jeu n'est jamais distribué —, l'émulateur restant la référence
qui prouve chaque fonction traduite. Plus rapide, plus léger, et la voie vers une édition iPad.
[Jalon](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/4)

- [ ] Banc d'équivalence : une fonction native vérifiée contre l'originale à chaque appel ([#26](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/26))
- [ ] Première fonction traduite, de l'x86-32 vers le C ([#42](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/42))
- [ ] Le recompilateur, puis tout le jeu sans émulateur ([#27](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/27))

### En exploration — la City Building Series
*Emperor: Rise of the Middle Kingdom* a été bâti sur le moteur de *Zeus*. Le même hôte
pourrait-il le faire revivre — et *Pharaoh*, *Caesar III* ? D'abord des mesures
([#28](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/28),
[#29](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/29)).

Le plan complet, ce que chaque étape doit atteindre et un retour sur la 1.0 :
[docs/roadmap.md](docs/roadmap.md) (en anglais).

## Historique des versions

| Version | Points forts |
|---|---|
| [**1.0.1**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0.1) | le jeu à sa vraie vitesse (il tournait aux deux tiers depuis la 0.2) ; répartiteur de sprites natif |
| [**1.0**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0) | première version stable : le code de la 0.9, joué à la main |
| [0.9](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.9) | version candidate : démarrage en moins d'une seconde, vraie résolution affichée en mode large, moins de mémoire |
| [0.4](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.4) | son 3D, routines de dessin natives, molette, pause en arrière-plan, essais automatiques bien plus larges |
| [0.3](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.3) | écran large ; packs de langue fabriqués par l'application depuis les CD d'origine |
| [0.2](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.2) | le français ; deux fois moins de processeur ; l'application ouvre elle-même l'installateur GOG |
| [0.1](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.1) | premier aperçu : menus, tutoriels, construction, sauvegarde et chargement, musiques, bruitages, vidéo d'introduction, plein écran |

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
routines les plus sollicitées du jeu — dessin des sprites, leur répartiteur, une boucle du
démarrage — tournent aussi en natif, chacune prouvée identique image par image. Le jeu voit un
dossier `C:\Zeus` privé : les lectures aboutissent dans vos fichiers du jeu, les écritures dans
un dossier séparé. Voir [docs/architecture.md](docs/architecture.md).

## Également dans ce dépôt

Le projet a commencé par l'étude des données et du code du jeu ; ce travail est documenté pour
quiconque fabrique des outils pour la communauté *Zeus* / *Poséidon* :

- **Formats de fichiers** — [`formats/sg3.md`](formats/sg3.md) (sprites, `.sg3` + `.555`),
  [`formats/map.md`](formats/map.md) (cartes et sauvegardes, `.map` / `.sav` / `.pak`) et
  [`formats/eng.md`](formats/eng.md) (textes et aide du jeu, `.eng`), chacun
  avec un lecteur de référence en Python ; d'autres à venir ([#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30)).
- **Outils d'extraction** — lecteurs en pur Python des images CD d'origine, des archives
  InstallShield et des installateurs de mises à jour Wise ([`tools/`](tools/)).
- **Visionneuses** — `zeus-sprites` (tous les sprites du jeu) et `zeus-map` (terrain
  isométrique d'une carte).
- **Notes de rétro-ingénierie** — imports, symboles, comportement du moteur
  ([`analysis/notes/`](analysis/notes/), [docs/reverse-engineering.md](docs/reverse-engineering.md)).

## Communauté

C'est un projet de joueurs, pour les joueurs. Venez dire comment le jeu tourne sur votre Mac,
poser vos questions ou proposer des idées dans les
[Discussions](https://github.com/nic0lasm/zeus-poseidon-arm64/discussions) ; les bugs vont dans
les [tickets](https://github.com/nic0lasm/zeus-poseidon-arm64/issues).

L'aide la plus utile aujourd'hui :

- **Jouer et raconter** — une aventure jouée jusqu'au bout, sur votre modèle de Mac.
- **Une autre édition linguistique ?** Essayez un pack depuis vos CD et dites-nous
  ([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)).
- **Du code** — les tickets [good first issue](https://github.com/nic0lasm/zeus-poseidon-arm64/labels/good%20first%20issue)
  et [help wanted](https://github.com/nic0lasm/zeus-poseidon-arm64/labels/help%20wanted) ; voir
  [CONTRIBUTING.md](CONTRIBUTING.md).

## Mentions légales

Projet de fans indépendant et non commercial, sans lien avec Impressions Games, Sierra,
Activision ou GOG, qui ne le soutiennent ni ne l'approuvent. *Zeus : Le Maître de l'Olympe* et
*Poséidon* sont des marques de leurs propriétaires respectifs.

Le dépôt ne contient que du code source original, des outils et de la documentation. Il ne
distribue aucun exécutable, image, son, texte ou donnée du jeu, et ne contourne aucune
protection : il n'exécute que l'édition GOG, sans DRM, à partir des fichiers que vous
fournissez.

## Licence

GPL-2.0-or-later — voir [LICENSE](LICENSE). Elle ne couvre que le code et la documentation de
ce dépôt, jamais le jeu.

L'émulation x86 repose sur [Unicorn](https://www.unicorn-engine.org/) (GPL-2.0) ; les vidéos
sont décodées par [FFmpeg](https://ffmpeg.org/). Voir [docs/third-party.md](docs/third-party.md).
