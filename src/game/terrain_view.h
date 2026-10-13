// Vue isométrique du terrain d'une carte : quelles images dessiner, où, et dans quel ordre.
#pragma once

#include <cstdint>
#include <vector>

#include "formats/mapfile.h"
#include "game/image_store.h"

namespace zeus::game {

constexpr int kGridSide = 228;        // côté de la grille de cases
constexpr int kTileWidth = 58;        // losange d'une case à l'écran
constexpr int kTileHeight = 30;
constexpr int kTileStepX = 30;        // décalage d'écran entre deux cases voisines
constexpr int kTileStepY = 15;

// Rang des blocs de la carte (adresses correspondantes dans Zeus.exe 2.1 entre parenthèses).
constexpr size_t kImageGridChunk = 4;  // u32 par case : identifiant d'image (0x01216248)
constexpr size_t kEdgeGridChunk = 5;   // u8 par case : bit 0x40 = case de dessin (0x01209738)

// Les cartes antérieures à Poséidon numérotent les collections sans les deux collections
// insérées depuis (Zeus_Unloaded et Poseidon_Unloaded) : le terrain y est la collection 3 et
// non 5. Constaté sur les 9 cartes en version 247 ; la conversion faite par le jeu au chargement
// reste à retrouver dans le code (hypothèse : +2 à partir de la collection 2).
constexpr uint32_t kFirstPoseidonMapVersion = 300;
uint32_t convert_image_id(uint32_t image_id, uint32_t map_version);

struct TerrainSprite {
    uint32_t image_id;
    int tile_x, tile_y;
    int x, y, width, height;  // rectangle à l'écran, dans le repère de la carte
};

struct TerrainView {
    std::vector<TerrainSprite> sprites;  // dans l'ordre de dessin, du fond vers l'avant
    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    size_t skipped = 0;  // cases dont l'image n'a pas pu être résolue
};

// Coin haut-gauche, à l'écran, du losange de la case (x, y).
inline int tile_screen_x(int x, int y) { return (x - y) * kTileStepX; }
inline int tile_screen_y(int x, int y) { return (x + y) * kTileStepY; }

TerrainView build_terrain_view(const formats::MapFile& map, ImageStore& images);

}  // namespace zeus::game
