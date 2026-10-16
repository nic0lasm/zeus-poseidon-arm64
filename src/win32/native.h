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

// Une boucle de dessin des personnages d'une case, celle de FUN_0045a5a0 et des deux boucles du
// second parcours de FUN_00458170 : tri de la liste (FUN_00449b30), puis FUN_0044fef0(personnage,
// x, y, cherché, plans) pour chaque personnage non masqué que le filtre retient.
struct TileWalkers {
    int32_t tile, x, y;
    uint32_t wanted;
    int32_t filter;  // 0 : tous ; 1 : ceux sur l'eau (FUN_004d7210) ; 2 : les autres ; autre : aucun
    uint32_t layers;
};
// Fait les boucles `loops` dans l'ordre (trois au plus) et renvoie vrai, ou renvoie faux sans avoir
// rien écrit quand l'une d'elles n'est pas un cas traité (native_walkers.cpp).
bool draw_tile_walkers(Cpu& cpu, const TileWalkers* loops, int count);

// Boucle de dessin de la ville (native_draw.cpp) ; renvoie le nombre de remplacements installés.
int install_native_drawing(Process& process);
// Personnages de la ville (native_walkers.cpp) ; renvoie le nombre de remplacements installés.
int install_native_walkers(Process& process);

// Aides des bâtiments composés (native_buildings.cpp) ; renvoie le nombre de remplacements installés.
int install_native_buildings(Process& process);
// Aides des bâtiments composés pour les autres réécritures (native_buildings.cpp), à n'utiliser
// que si composites_known() : FUN_005344a0 ; FUN_0053c2c0(this, type) ; le décalage de la pièce
// `part` tourné, sans les écritures de FUN_00546620 ; FUN_0053f810(this ; type, x, y, orientation,
// &x, &y), `entry` étant ESP à son entrée (rien si une classe de pièce n'a pas la forme attendue).
bool composite_type(int32_t type);
uint32_t composite_parts(Cpu& cpu, uint32_t self, int32_t type);
void composite_offset(Cpu& cpu, uint32_t part, uint32_t orientation, int32_t& x, int32_t& y);
std::optional<uint32_t> composite_road_entry(Cpu& cpu, uint32_t self, int32_t type, int32_t x, int32_t y,
                                             uint32_t orientation, uint32_t out_x, uint32_t out_y, uint32_t entry);
bool composites_known();
// Recherche de routes de la simulation (native_routes.cpp) ; renvoie le nombre de remplacements installés.
int install_native_routes(Process& process);
// Personnages de la simulation (native_figures.cpp) ; renvoie le nombre de remplacements installés.
int install_native_figures(Process& process);
// Fonctions du terrain (native_terrain.cpp) ; renvoie le nombre de remplacements installés.
int install_native_terrain(Process& process);
// Recherche des chaînes et mise en page des textes des panneaux (native_text.cpp) ; renvoie le
// nombre de remplacements installés.
int install_native_text(Process& process);
// Dessin des chaînes d'une ligne (native_strings.cpp) ; renvoie le nombre de remplacements installés.
int install_native_strings(Process& process);

// FUN_004170b0 : saute les lignes masquées en haut d'une image par suites (native.cpp) ; renvoie
// l'adresse de la première ligne visible.
uint32_t skip_run_rows(Cpu& cpu, uint32_t image);
// FUN_00413e30(image, transparence) : image par suites mélangée au fond (native.cpp).
void draw_runs_blended_whole(Cpu& cpu, uint32_t image, uint32_t table);
// Recouvrement et transparence (native_blending.cpp), à n'utiliser que si blending_known() :
//   - overlay_runs : fond &= couleur sous une image par suites, FUN_00416820, rognée à gauche
//     (`clip_x` 1) FUN_00416af0, à droite (2) FUN_00416b90 ;
//   - draw_runs_mirrored_coloured : image par suites retournée redessinée avec source & couleur,
//     FUN_00415b80, FUN_00415d30 (1), FUN_00415c40 (2) ;
//   - draw_runs_blended_variant : FUN_00414ce0 (1), FUN_00414540 (2), retournée FUN_00414190 (0),
//     FUN_004150b0 (1), FUN_00414910 (2) ; non retournée et entière, FUN_00413e30 ;
//   - draw_glyph_mask : FUN_00416c30, masque de glyphe non rogné, couleur kGlyphColour.
bool blending_known();
void draw_glyph_mask(Cpu& cpu, uint32_t image);
void overlay_runs(Cpu& cpu, uint32_t image, uint16_t colour, uint8_t clip_x);
void draw_runs_mirrored_coloured(Cpu& cpu, uint32_t image, uint16_t colour, uint8_t clip_x);
void draw_runs_blended_variant(Cpu& cpu, uint32_t image, uint32_t table, bool mirror, uint8_t clip_x);
int install_native_blending(Process& process);

}  // namespace zeus::win32
