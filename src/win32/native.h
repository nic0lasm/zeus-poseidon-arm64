// Fonctions du jeu réécrites pour l'hôte, partagées entre native.cpp et native_draw.cpp.
#pragma once

#include <cstdint>
#include <optional>

#include "win32/cpu.h"

namespace zeus::win32 {

class Process;

// Répartiteur de sprites FUN_005acf50(this, identifiant, x, y, mode, couleur 0, miroir), pour les
// cas qu'il traite en natif (native.cpp) : dessine le sprite et renvoie la valeur de retour de
// l'original, ou ne renvoie rien sans avoir rien écrit.
std::optional<uint32_t> draw_sprite(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y, int32_t mode,
                                    uint8_t mirror);

// Vrai quand draw_sprite traiterait ce sprite (rien n'est écrit) : un appelant qui dessine
// plusieurs sprites le vérifie pour tous avant le premier.
bool sprite_handled(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y, int32_t mode, uint8_t mirror);

// FUN_005adf00(this, identifiant, x, y) : partie haute d'un bâtiment (native.cpp), même contrat.
std::optional<uint32_t> draw_building_top(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y);

// FUN_00435070(image, rognage vertical) sans surimpression : sol en losange de 58 x 30 pixels,
// image opaque rangée ligne par ligne, à la position kTargetX, kTargetY (native_draw.cpp).
// `ground_known` : vrai quand le code de FUN_00435070 est celui attendu (install_native_drawing).
void draw_ground(Cpu& cpu, uint32_t image, uint8_t clip_y);
bool ground_known();
// FUN_004377b0 / FUN_00438a90 sans surimpression : losange rogné à gauche (`left`) ou à droite.
void draw_ground_half(Cpu& cpu, uint32_t image, uint8_t clip_y, bool left);
bool ground_halves_known();
bool bases_known();  // FUN_0043ff10 à FUN_00441e50, socles de 2 à 6 cases (native.cpp)

// Boucle de dessin de la ville (native_draw.cpp) ; renvoie le nombre de remplacements installés.
int install_native_drawing(Process& process);
// Personnages de la ville (native_walkers.cpp) ; renvoie le nombre de remplacements installés.
int install_native_walkers(Process& process);

}  // namespace zeus::win32
