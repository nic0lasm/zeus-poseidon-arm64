# `.pak` et `.set` — Aventures

Lecteurs : aucun pour le moment. Ces fichiers constituent le système d'aventure du jeu.

## État au 2026-10-14

Les fichiers `.pak` et `.set` sont utilisés pour les aventures (scénarios).
Ils sont distincts des formats `.map` (cartes) et `.eng` (textes).

| Fichier | Extension | Rôle présumé | Statut |
|---|---|---|---|
| `.pak` | Aventures/Scénarios | Conteneur de données pour les aventures | Non décodé |
| `.set` | Aventures/Scénarios | Configuration ou paramètres pour les aventures | Non décodé |

Une analyse préliminaire binaire des fichiers `.pak` montre une structure différente des fichiers `.map` (qui commencent par `MAPS`).

### Notes sur la structure
- Les fichiers `.pak` et `.set` ne présentent pas les signatures `MAPS` (cartes) ou `Zeus textfile.` (textes).
- Ils sont partie intégrante du système de chargement des aventures dans le jeu.
- Le décodage de ces formats reste à réaliser.

## Fichiers recensés

Le répertoire `original/patch-zeus-1.1/` contient :
- `.pak` : `017__Zeus et Europe.pak`, `019__Jeu ouvert - Economie.pak`, `020__Jeu ouvert - Arm_e.pak`, `018__Les travaux d_Hercule.pak`, `044_Jeu ouvert - Bac _ sable 2.pak`, `040_Jeu ouvert - Arm_e 2.pak`, `030__Jeu ouvert - Bac _ sable.pak`, `035_Jeu ouvert - Economie 2.pak`, `047_L_odyss_e.pak`
- `.set` : `041_Jeu ouvert - Arm_e 2.set`, `036_Jeu ouvert - Economie 2.set`, `045_Jeu ouvert - Bac _ sable 2.set`, `048_L_odyss_e.set`

*(Attribution : Co-Authored-By: Claude Code <noreply@anthropic.com>)*
