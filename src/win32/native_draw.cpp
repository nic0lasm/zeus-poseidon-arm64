// Dessin de la ville réécrit pour l'hôte : la boucle FUN_00458170 et le sol de chaque case.
//
// FUN_00458170 est appelée à chaque tour de simulation (45 fois par seconde à 90 %) et parcourt
// deux fois les quelque 1 300 cases visibles : d'abord le sol, puis ce qui s'y dresse. Dans une
// petite ville, le profil en temps réel lui attribue 17 % du calcul, et 4 % à FUN_00457900, le
// sol d'une case, qu'elle appelle pour presque chaque case du premier parcours.
//
// Le premier parcours est repris ici case par case : un aiguillage (Cpu::add_code_jump) en tête de
// la boucle traite en natif les cases courantes, puis rend la main à l'original à l'un de trois
// endroits, avec les registres et les variables qu'il y aurait laissés :
//   - en fin de rangée ;
//   - devant l'appel de FUN_00457900, quand le sol de la case n'est pas un cas traité ;
//   - en tête de boucle, pour une case que l'original fait entièrement (appels à d'autres
//     fonctions du jeu, première image de la partie).
// FUN_00457900 a aussi un chemin rapide (Cpu::add_fast_path) pour ses autres appels. Le sol
// lui-même, un losange de 58 x 30 pixels (FUN_00435070), est dessiné par le répartiteur de
// sprites natif (native.cpp) avec draw_ground.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si tous les octets réécrits ici sont
// ceux attendus (empreinte FNV-1a, comparée avant toute modification).

#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

// Lectures et écritures dans la mémoire du jeu, aux largeurs de l'original. Les adresses sont
// calculées en 32 bits, comme dans le jeu, y compris pour un indice négatif.
uint32_t u32(Cpu& cpu, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, cpu.host(address), 4);
    return value;
}
int32_t i32(Cpu& cpu, uint32_t address) { return static_cast<int32_t>(u32(cpu, address)); }
int32_t s16(Cpu& cpu, uint32_t address) {
    int16_t value;
    std::memcpy(&value, cpu.host(address), 2);
    return value;
}
uint16_t u16(Cpu& cpu, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, cpu.host(address), 2);
    return value;
}
int32_t s8(Cpu& cpu, uint32_t address) { return static_cast<int8_t>(*cpu.host(address)); }
uint8_t u8(Cpu& cpu, uint32_t address) { return *cpu.host(address); }
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }
void put16(Cpu& cpu, uint32_t address, uint16_t value) { std::memcpy(cpu.host(address), &value, 2); }
void put8(Cpu& cpu, uint32_t address, uint8_t value) { *cpu.host(address) = value; }

uint32_t at(uint32_t base, int32_t index, uint32_t size) { return base + static_cast<uint32_t>(index) * size; }

constexpr int32_t kColumns = 0xE7;      // cases par rangée de la grille
constexpr int32_t kMark = 0x0FFF0000;   // à partir de là, la grille contient une marque, pas une case

// Premier identifiant d'image du groupe `group` de l'interface du terrain, ou du groupe 0 quand
// il n'existe pas (FUN_00457900 : « DAT_012fc574 < n ? groupe 0 : groupe n »).
int32_t terrain_group(Cpu& cpu, int32_t group) {
    const uint32_t groups = u32(cpu, kTerrainGroups);
    return i32(cpu, (i32(cpu, kTerrainGroupCount) < group ? groups : at(groups, group, 8)) + 4);
}

// Orientation de la vue 2 ou 6 : FUN_0045dc10 et les variantes retournées (FUN_005ae1b0,
// FUN_0045a790, FUN_005ae9f0) ne sont pas réécrites.
bool turned(Cpu& cpu) {
    const int32_t orientation = i32(cpu, kViewOrientation);
    return orientation == 2 || orientation == 6;
}

// FUN_0045daf0(case) : un personnage visible de certains types se tient sur la case.
bool figure_behind(Cpu& cpu, int32_t tile) {
    if (tile >= kMark || u8(cpu, at(kTileHidden, tile, 1)) != 0) return false;
    for (int32_t figure = s16(cpu, at(kTileFigure, tile, 2)); figure > 0;
         figure = s16(cpu, at(kFigures, figure, 0x184))) {
        if (u8(cpu, at(kFigures + 0x5D, figure, 0x184)) != 0) continue;
        switch (u8(cpu, at(kFigures + 2, figure, 0x184))) {
            case 0x01: case 0x04: case 0x0D: case 0x1C: case 0x2A: case 0x43: case 0x44: case 0x45:
                return true;
            default: break;
        }
    }
    return false;
}

// FUN_00457900, fin : un personnage de certains types sur l'une des deux cases de la rangée
// précédente qui touchent celle-ci (selon la parité de la rangée) se dessine par FUN_005adf00.
bool walker_behind(Cpu& cpu, uint32_t odd) {
    const uint32_t cell = at(kTileGrid, kColumns * i32(cpu, kViewRow) + i32(cpu, kViewColumn), 4);
    return figure_behind(cpu, i32(cpu, cell - (odd ? 0x3A0 : 0x39C))) ||
           figure_behind(cpu, i32(cpu, cell - (odd ? 0x39C : 0x398)));
}

// Décalage d'un morceau en pente qui couvre `slope` cases (1 à 5), en pas de case : FUN_0045a690
// et FUN_00457900 (tables 0x0045A774, 0x00458128, 0x00458150). Au-delà de 5, aucun.
void slope_offset(Cpu& cpu, uint32_t slope, int32_t& x, int32_t& y) {
    switch (slope) {
        case 1: x += i32(cpu, kHalfTileWidth) + 1; y -= i32(cpu, kHalfTileHeight); break;
        case 2: x += i32(cpu, kTileStep) + 2; y -= i32(cpu, kTileHeightStep); break;
        case 3: x += i32(cpu, kTileWidth15) + 3; y -= i32(cpu, kTileHeight15); break;
        case 4: x += i32(cpu, kTileWidth2) + 4; y -= i32(cpu, kTileHeight2); break;
        case 5:
            x += i32(cpu, kTileWidth2) + i32(cpu, kHalfTileWidth) + 5;
            y -= i32(cpu, kTileHeight2) + i32(cpu, kHalfTileHeight);
            break;
        default: break;
    }
}

// FUN_00457900(case, x, y, rangée impaire) : le sol d'une case. Seul le cas courant est traité :
// mode de vue 0, terrain plat, une image passée au répartiteur de sprites. Pour tout autre cas,
// la fonction renvoie faux sans avoir rien écrit, et l'original fait le travail.
bool ground_tile(Cpu& cpu, int32_t tile, int32_t x, int32_t y, uint32_t odd) {
    if (tile < 0 || tile >= kMark) return false;
    const uint32_t mode = u32(cpu, kViewMode);
    // Méthode +0x18 de l'objet du mode de vue, appelée avec la nature du terrain.
    const uint32_t view = u32(cpu, at(kViewModes, static_cast<int32_t>(mode), 4));
    if (u32(cpu, u32(cpu, view) + 0x18) != kAlwaysTrue) return false;
    const uint32_t flags = u32(cpu, at(kTileFlags, tile, 4));
    if ((u8(cpu, at(kTileBits, tile, 1)) & 0x40) == 0) return true;  // rien à dessiner
    // Bâtiment en rouge (surimpression) ou vue tournée (FUN_0045dc10, image retournée) : non traités.
    const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
    if (building != 0 && (u8(cpu, at(kBuildings + 0xBF, building, 0x118)) != 0 || turned(cpu))) return false;
    if (i32(cpu, kHighlight) == 1) return false;  // FUN_0051ef30
    const uint8_t slope = u8(cpu, at(kTileSlope, tile, 1));
    int32_t image = i32(cpu, at(kTileImage, tile, 4));
    if (slope & 0x10) image = terrain_group(cpu, 12) + 0x14000;
    // Morceau en pente (0x00457D9D) : la même image, décalée selon sa taille ; ni eau animée, ni
    // personnage derrière. Vue tournée : FUN_005ae9f0 ; drapeau 8 hors du mode 0 : FUN_0044b050.
    if (slope & 0x0F) {
        if ((flags & 0x20) ? turned(cpu) : ((flags & 0x08) && mode != 0)) return false;
        slope_offset(cpu, slope & 0x0F, x, y);
        if (!draw_sprite(cpu, kSpriteBanks, image, x, y, 0, 0)) return false;
        put32(cpu, kDrawnImage, static_cast<uint32_t>(image));
        put16(cpu, kOverlay, 0);
        return true;
    }
    // Eau animée : l'image avance d'un pas dans son cycle, et la case garde la nouvelle image.
    bool animated = false;
    if ((flags & 0x04) && u32(cpu, kWaterMoves) != 0) {
        const bool deep = (flags & 0x04000000) != 0;
        const int32_t first = terrain_group(cpu, deep ? 19 : 34) + 0x14000;
        const int32_t step = deep ? 0x0F : 1, last = first + step * 6 - 1;
        if (image <= last && image >= first) {
            image += step;
            if (image > last) image -= deep ? 0x5A : 6;
            animated = true;
        }
    }
    if (flags & 0x01000000) return false;  // FUN_00507980
    // Drapeaux 0x4000 ou 0x20 (0x00457D64) : l'image à sa place, sans personnage derrière.
    if (flags & 0x4020) {
        if (turned(cpu)) return false;  // FUN_005ae9f0
        if (!draw_sprite(cpu, kSpriteBanks, image, x, y, 0, 0)) return false;
        put32(cpu, kDrawnImage, static_cast<uint32_t>(image));
        if (animated) put32(cpu, at(kTileImage, tile, 4), static_cast<uint32_t>(image));
        put16(cpu, kOverlay, 0);
        return true;
    }
    if ((flags & 0x08) && mode != 0) return false;  // FUN_0044b050
    if (mode == 0x11) return false;  // FUN_0044b830
    if ((flags & 0x2DFED66F) == 0 && walker_behind(cpu, odd)) return false;
    if (!draw_sprite(cpu, kSpriteBanks, image, x, y, 0, 0)) return false;
    put32(cpu, kDrawnImage, static_cast<uint32_t>(image));
    if (animated) put32(cpu, at(kTileImage, tile, 4), static_cast<uint32_t>(image));
    put16(cpu, kOverlay, 0);
    return true;
}

// Variables locales de FUN_00458170 pendant le parcours du sol (ESP après le prologue).
constexpr uint32_t kX = 0x10, kOdd = 0x14, kBaseY = 0x18, kCompare = 0x1C, kCounter = 0x20, kLift = 0x28,
                   kTileCopy = 0x3C;

enum class Cell { Done, Call, Original };

// Une case du parcours du sol (0x0045829D à 0x0045864A).
Cell ground_cell(Cpu& cpu, uint32_t esp, int32_t row, int32_t column, int32_t& tile) {
    const int32_t index = kColumns * row + column;
    tile = i32(cpu, at(kTileGrid, index, 4));
    if (tile < 0 || tile >= kMark) return Cell::Original;
    put32(cpu, kViewTile, static_cast<uint32_t>(tile));
    const int32_t height = s8(cpu, at(kTileHeight, tile, 1));
    const uint32_t image = u32(cpu, at(kTileImage, tile, 4));
    put32(cpu, kDrawnImage, image);
    put32(cpu, esp + kLift, static_cast<uint32_t>(height * 30));
    // Entrée de l'image dans sa banque : banque = image / 16384, numéro = image % 16384, signés.
    const int32_t bank = static_cast<int32_t>(image) / 16384, number = static_cast<int32_t>(image) % 16384;
    const uint32_t banks = at(kSpriteBanks, bank, 0xCC44);
    const uint32_t table = u32(cpu, banks + 0x1000);
    const uint32_t entry = number > i32(cpu, banks + 0xDC34) ? table : at(table, number, 0x48);
    // Hauteur comparée à celle de la case deux rangées plus haut.
    int32_t compare = 0;
    if (row >= 2) {
        const int32_t up = i32(cpu, at(kTileGrid, index - 2 * kColumns, 4));
        if (up < kMark) {
            const int32_t up_height = s8(cpu, at(kTileHeight, up, 1));
            if (up_height > height)
                compare = 1;
            else if (up_height < height)
                compare = -1;
            else if ((u32(cpu, at(kTileFlags, tile, 4)) & 0x200) && (u32(cpu, at(kTileFlags, up, 4)) & 0x800200))
                compare = 1;
        }
    }
    put32(cpu, esp + kCompare, static_cast<uint32_t>(compare));
    if (u8(cpu, entry + 0x35) != 0) {
        const uint32_t flags = u32(cpu, at(kTileFlags, tile, 4));
        const bool kept = (u8(cpu, at(kTileSlope, tile, 1)) & 0x0F) == 0 && (flags & 0x881) && !(flags & 0x1000C208);
        if (!kept) {
            const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
            if (building != 0 && (u8(cpu, at(kTileBits, tile, 1)) & 0x40)) {
                switch (s16(cpu, at(kBuildings, building, 0x118))) {
                    case 2: case 9: case 0x7B: case 0x86: break;
                    default: return Cell::Original;  // FUN_005344a0, FUN_00546530
                }
            } else if (compare != 1) {
                return Cell::Done;
            }
        }
    }
    if (compare == -1 || u32(cpu, kViewMode) == 0x11) return Cell::Done;
    put32(cpu, esp + kTileCopy, static_cast<uint32_t>(tile));
    if (u8(cpu, kSkipReady) == 0) return Cell::Original;  // préparation : FUN_0040fcf0, FUN_0040fd10
    const uint32_t ground = u32(cpu, at(kTileImage, tile, 4)), first = u32(cpu, kSkipFirst);
    if (ground >= first && ground <= u32(cpu, kSkipLast)) {
        const uint32_t bit = ground - first;
        if (u32(cpu, kSkipBits + (bit >> 5) * 4) & (1u << (bit & 31))) return Cell::Done;
    }
    const int32_t y = i32(cpu, esp + kBaseY) - height * 30;
    return ground_tile(cpu, tile, i32(cpu, esp + kX), y, u32(cpu, esp + kOdd)) ? Cell::Done : Cell::Call;
}

// Aiguillage en tête de la boucle du sol : EBX est la rangée, la colonne est kViewColumn.
uint32_t ground_row(Cpu& cpu) {
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    const int32_t row = static_cast<int32_t>(cpu.reg(Cpu::Reg::EBX));
    for (;;) {
        const int32_t column = i32(cpu, kViewColumn);
        if (column >= 0 && column < kColumns) {
            int32_t tile = 0;
            switch (ground_cell(cpu, esp, row, column, tile)) {
                case Cell::Done: break;
                case Cell::Original: return 0;
                case Cell::Call: cpu.set_reg(Cpu::Reg::EDI, static_cast<uint32_t>(tile)); return kGroundCall;
            }
        }
        // Case suivante (0x00458653).
        put32(cpu, esp + kX, u32(cpu, esp + kX) + u32(cpu, kTileStep) + 2);
        const int32_t counter = i32(cpu, esp + kCounter) + 1;
        put32(cpu, esp + kCounter, static_cast<uint32_t>(counter));
        put32(cpu, kViewColumn, u32(cpu, kViewColumn) + 1);
        const int32_t columns = i32(cpu, kViewWidth);
        if (counter > columns + 8) {
            cpu.set_reg(Cpu::Reg::EAX, static_cast<uint32_t>(counter));
            cpu.set_reg(Cpu::Reg::ECX, static_cast<uint32_t>(columns));
            cpu.set_reg(Cpu::Reg::EDX, static_cast<uint32_t>(columns + 8));
            return kGroundRowEnd;
        }
    }
}

std::optional<uint32_t> ground_tile_call(Cpu& cpu) {
    if (!ground_tile(cpu, static_cast<int32_t>(cpu.arg(0)), static_cast<int32_t>(cpu.arg(1)),
                     static_cast<int32_t>(cpu.arg(2)), cpu.arg(3)))
        return std::nullopt;
    return 1u;
}

// --- Second parcours : ce qui se dresse sur chaque case (0x0045877E à 0x0045A01C) -----------
//
// Même principe que le sol : un aiguillage en 0x00458792 (case dans la grille) traite les cases
// courantes en natif, case après case, puis rend la main à l'original à un point de reprise,
// avec les registres et les variables que l'original y aurait. Chaque étape de la case décide
// d'abord, sans rien écrire, si elle sait faire ; sinon elle reprend au début de l'étape :
//   - kObjectsCell (début de case, octets déplacés de l'aiguillage), kAfterAmbient (0x00458D7A),
//     kAfterObject (0x004594FA), kAfterAnimation (0x00459CE2), kViewOverlay (0x00459D23),
//     kWalkers (0x00459D7A) ;
//   - en fin de rangée, 0x0045A022.
// Seul le mode de vue 0 (la ville) est traité ; ses méthodes +0x0C (« faux ») et +0x20 sont
// vérifiées à l'installation.

// Variables locales de FUN_00458170 pendant le second parcours (ESP après le prologue).
constexpr uint32_t kObjectRowCounter = 0x24, kObjectBuilding = 0x30, kRingNow = 0x34, kRingTwo = 0x38,
                   kRingFour = 0x3C, kCentreColumn = 0x40, kCentreRow = 0x44;

// Ordinateur de la vue : FUN_004200f0(0), vrai pour une partie de Poséidon (hypothèse : le nom
// vient des tables qu'il choisit, 0x005E86B0 pour Poséidon).
bool poseidon(Cpu& cpu) { return u32(cpu, kGameMode) == 1 && (u32(cpu, kGameSubmode) & 0xFFFF) == 1; }

// FUN_0040aa20(type) : le type de bâtiment figure dans la table de 6 entrées de 0x34 octets.
bool listed_building(Cpu& cpu, int32_t type) {
    for (int32_t i = 5; i >= 0; --i) {
        const uint32_t entry = i == 5 && poseidon(cpu) ? kPoseidonBuildings : at(kBuildingTable, i, 0x34);
        if (i32(cpu, entry + 4) == type) return true;
    }
    return false;
}

int32_t building_type(Cpu& cpu, int32_t building) { return s16(cpu, at(kBuildings, building, 0x118)); }


// FUN_0045a690(pente, image, x, y) : partie haute d'un morceau en pente, décalée selon sa taille.
std::optional<uint32_t> sloped_top(Cpu& cpu, uint32_t slope, int32_t image, int32_t x, int32_t y) {
    slope_offset(cpu, slope, x, y);
    return draw_building_top(cpu, kSpriteBanks, image, x, y);
}

// Une case du second parcours, à partir de l'étape `from` ; renvoie 0 quand la case est finie, ou
// l'adresse où l'original la poursuit (registres déjà placés).
struct ObjectCell {
    int32_t row, column, tile, x, y;
    uint32_t odd;
};

// Étape 1 (0x004587B9 à 0x00458C3C) : en-tête de la case et sol redessiné. Renvoie faux quand
// l'original doit faire la case depuis son début (rien n'a été écrit qu'il ne réécrive pareil).
bool objects_ground(Cpu& cpu, uint32_t esp, ObjectCell& c) {
    const int32_t tile = c.tile;
    const int32_t height = s8(cpu, at(kTileHeight, tile, 1));
    const uint32_t image = u32(cpu, at(kTileImage, tile, 4));
    const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
    put32(cpu, kDrawnImage, image);
    put32(cpu, esp + kLift, static_cast<uint32_t>(height * 30));
    put32(cpu, esp + kObjectBuilding, static_cast<uint32_t>(building));
    c.y = i32(cpu, esp + kBaseY) - height * 30;
    const int32_t bank = static_cast<int32_t>(image) / 16384, number = static_cast<int32_t>(image) % 16384;
    const uint32_t banks = at(kSpriteBanks, bank, 0xCC44);
    const uint32_t table = u32(cpu, banks + 0x1000);
    const uint32_t entry = number > i32(cpu, banks + 0xDC34) ? table : at(table, number, 0x48);
    const int32_t index = kColumns * c.row + c.column;
    int32_t compare = 0;
    if (c.row >= 2) {
        const int32_t up = i32(cpu, at(kTileGrid, index - 2 * kColumns, 4));
        if (up < kMark) {
            const int32_t up_height = s8(cpu, at(kTileHeight, up, 1));
            if (up_height > height)
                compare = 1;
            else if (up_height < height)
                compare = -1;
            else if ((u32(cpu, at(kTileFlags, tile, 4)) & 0x200) && (u32(cpu, at(kTileFlags, up, 4)) & 0x800200))
                compare = 1;
        }
    }
    const uint32_t flags = u32(cpu, at(kTileFlags, tile, 4));
    const uint8_t slope = u8(cpu, at(kTileSlope, tile, 1)) & 0x0F;
    // Faut-il redessiner le sol de la case, et faut-il alors effacer son drapeau 0x40000000 ?
    bool redraw = false, clear = false, raised = false;
    bool kept = u8(cpu, entry + 0x35) == 0 || (slope == 0 && (flags & 0x881) && !(flags & 0x1000C208));
    if (!kept && building != 0 && (u8(cpu, at(kTileBits, tile, 1)) & 0x40)) {
        const int32_t type = building_type(cpu, building);
        switch (type) {
            case 2: case 9: case 0x7B: case 0x86: kept = true; break;
            default:
                // FUN_005344a0 ou 54..62, 75, 64..72 : FUN_00546530, méthode virtuelle d'un objet.
                if ((type > 0x53 && (type < 99 || type == 0x75)) || (type >= 100 && type <= 0x72)) return false;
                break;
        }
    }
    if (kept || compare == 1) {
        raised = compare != -1;
        if (raised) {
            if (flags & 0x40000000) {
                redraw = clear = true;
            } else if (height > 0) {
                int32_t up_row = c.row - 2, level = height - 1;
                for (int32_t k = 1; k <= height; ++k, up_row -= 2, --level) {
                    if (up_row < 0) break;
                    const int32_t up = i32(cpu, at(kTileGrid, kColumns * up_row + c.column, 4));
                    if (up >= kMark) break;
                    if (s8(cpu, at(kTileHeight, up, 1)) <= level) {
                        redraw = clear = true;
                        break;
                    }
                }
            }
        }
    }
    if (!raised) {
        // 0x00458A19 : redessiné sauf si une case deux rangées devant est plus haute, ou quatre
        // rangées devant plus haute de deux crans ; puis la liste des images que la boucle saute.
        bool hidden_in_front = false;
        if (slope == 0 && c.row + 2 < 0x1C9) {
            const int32_t front = i32(cpu, at(kTileGrid, index + 2 * kColumns, 4));
            if (front < kMark) {
                if (s8(cpu, at(kTileHeight, front, 1)) > height) {
                    hidden_in_front = true;
                } else if (c.row + 4 < 0x1C9) {
                    const int32_t far = i32(cpu, at(kTileGrid, index + 4 * kColumns, 4));
                    if (far < kMark && s8(cpu, at(kTileHeight, far, 1)) > height + 1) hidden_in_front = true;
                }
            }
        }
        if (!hidden_in_front) {
            if (u8(cpu, kSkipReady) == 0) return false;  // préparation : FUN_0040fcf0, FUN_0040fd10
            redraw = true;
            const uint32_t ground = u32(cpu, at(kTileImage, tile, 4)), first = u32(cpu, kSkipFirst);
            if (ground >= first && ground <= u32(cpu, kSkipLast)) {
                const uint32_t bit = ground - first;
                if (u32(cpu, kSkipBits + (bit >> 5) * 4) & (1u << (bit & 31))) redraw = false;
            }
        }
    }
    if (redraw) {
        if (!ground_tile(cpu, tile, c.x, c.y, c.odd)) return false;
        if (clear) put32(cpu, at(kTileFlags, tile, 4), u32(cpu, at(kTileFlags, tile, 4)) & 0xBFFFFFFF);
    }
    return true;
}

// FUN_005a7200(type) : classe d'un bâtiment pour les compteurs d'ambiance (0 : aucune).
int32_t ambient_class(int32_t type) {
    switch (type) {
        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 0x3F: case 0x40: case 0x4A: case 0x4C:
        case 0x4D: case 0x4E: case 0x4F: case 0x50: case 0x51: case 0x52: case 0x79: case 0x7C: case 0x7D:
        case 0x7E: case 0x7F: case 0x80: case 0x82: case 0x83: case 0x84: case 0x85:
            return 1;
        case 0x1F: case 0x20: case 0x21: case 0x24: case 0x25: case 0x2A: case 0x7A:
            return 3;
        case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x30: case 0x31: case 0x32: case 0x33: case 0x35:
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x87: case 0x88:
            return 2;
        case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C:
        case 0x5D: case 0x5E: case 0x5F: case 0x62: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68:
        case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F: case 0x70: case 0x71:
        case 0x72:
            return 4;
        default: return 0;
    }
}

void increment(Cpu& cpu, uint32_t address) { put32(cpu, address, u32(cpu, address) + 1); }

// Étape 2 (0x00458C3C à 0x00458D7A) : compteurs d'ambiance, pour les cases proches du centre.
void objects_ambient(Cpu& cpu, uint32_t esp, const ObjectCell& c) {
    const int32_t centre_column = i32(cpu, esp + kCentreColumn), centre_row = i32(cpu, esp + kCentreRow);
    const int32_t counter = i32(cpu, esp + kCounter), rows = i32(cpu, esp + kObjectRowCounter);
    if (counter <= centre_column - 3 || counter >= centre_column + 3 || rows <= centre_row - 3 ||
        rows >= centre_row + 3)
        return;
    const int32_t building = i32(cpu, esp + kObjectBuilding);
    if (building != 0) {
        switch (ambient_class(building_type(cpu, building))) {
            case 1: increment(cpu, kAmbientCounters + 0x00); break;
            case 2: increment(cpu, kAmbientCounters + 0x04); break;
            case 4: increment(cpu, kAmbientCounters + 0x0C); break;
            default: break;
        }
        return;
    }
    const uint32_t flags = u32(cpu, at(kTileFlags, c.tile, 4));
    if ((flags & 0x04) && !(flags & 0x80000)) increment(cpu, kAmbientCounters + 0x24);
    else if (flags & 0x300002) increment(cpu, kAmbientCounters + 0x20);
    else if (flags & 0x800) increment(cpu, kAmbientCounters + 0x18);
    else if (flags & 0x50000) increment(cpu, kAmbientCounters + 0x1C);
    else if (flags & 0x80) increment(cpu, kAmbientCounters + 0x18);
    else increment(cpu, kAmbientCounters + 0x14);
}

// Étape 3 (0x00458D7A à 0x004594FA) : l'objet posé sur une case d'ancrage, en mode de vue 0.
bool objects_object(Cpu& cpu, const ObjectCell& c) {
    const int32_t tile = c.tile;
    const uint32_t flags = u32(cpu, at(kTileFlags, tile, 4));
    const bool life = u8(cpu, at(kTileLife, tile, 1)) != 0;
    if (life && ((flags & 0x44) == 0x44 ||
                 ((flags & 0x48) == 0x48 && building_type(cpu, s16(cpu, at(kTileBuilding, tile, 2))) == 0xC7)))
        return false;  // FUN_0041d150 : bateau ou animal sur l'eau
    if ((u8(cpu, at(kTileBits, tile, 1)) & 0x40) == 0) return true;
    const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
    if (building != 0 && u8(cpu, at(kBuildings + 0xBF, building, 0x118)) != 0) return false;  // en rouge
    if (u16(cpu, kOverlay) != 0) return false;
    const int32_t image = i32(cpu, at(kTileImage, tile, 4));
    const uint32_t slope = u8(cpu, at(kTileSlope, tile, 1)) & 0x0F;
    enum class Top { None, Flat, Sloped } top = Top::Flat;
    if (slope == 0) {
        const int32_t index = kColumns * c.row + c.column;
        const int32_t first = i32(cpu, at(kTileGrid, index - kColumns - (c.odd ? 1 : 0), 4));
        const int32_t second = i32(cpu, at(kTileGrid, index - kColumns + (c.odd ? 0 : 1), 4));
        if ((flags & 0x2DFED66F) == 0 && (figure_behind(cpu, first) || figure_behind(cpu, second))) {
            top = Top::None;
        } else {
            if (flags & 0x1000000) return false;  // FUN_0045dbd0, méthode virtuelle
            if ((flags & 0x4020) ? turned(cpu) : (turned(cpu) && building != 0)) return false;
        }
    } else {
        if (turned(cpu) && ((flags & 0x20) || building != 0)) return false;
        top = Top::Sloped;
        if (slope == 2) {
            switch (building_type(cpu, building)) {  // FUN_004a9680, puis FUN_004a9f50
                case 0x1F: case 0x20: case 0x21: case 0x23: case 0x24: case 0xD9: return false;
                default: break;
            }
        }
    }
    const int32_t type = building_type(cpu, building);
    if ((type > 0x53 && (type < 99 || type == 0x75)) || (type > 99 && type < 0x73))
        return false;  // FUN_0053e5f0 : sanctuaires et monuments
    if (type == 0x81 || type == 0x91 || type == 0x92) return false;  // les deux tours, FUN_0041fd40
    put32(cpu, kDrawnImage, static_cast<uint32_t>(image));
    if (top == Top::Flat) draw_building_top(cpu, kSpriteBanks, image, c.x, c.y);
    else if (top == Top::Sloped) sloped_top(cpu, slope, image, c.x, c.y);
    put16(cpu, kOverlay, 0);
    return true;
}

// Types de bâtiment que le switch de 0x00459653 n'envoie pas au cas général (0x004597FD) : table
// 0x0045A374 / 0x0045A3A4, vérifiée à l'installation.
bool special_building(int32_t type) {
    switch (type) {
        case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x12: case 0x2C: case 0x2D: case 0x31: case 0x3A:
        case 0x3C: case 0x3D: case 0x3E: case 0x50: case 0x52: case 0x7D: case 0x88: case 0xC7: case 0xD4:
        case 0xD5: case 0xD8:
            return true;
        default: return false;
    }
}

// Types que FUN_0044f320 dessine (table 0x0044FD6C / 0x0044FDC4) ; pour les autres, elle ne fait rien.
bool building_extra(int32_t type) {
    switch (type) {
        case 0x25: case 0x29: case 0x2A: case 0x2E: case 0x30: case 0x31: case 0x32: case 0x33: case 0x35:
        case 0x36: case 0x37: case 0x7D: case 0x84: case 0x85: case 0x87: case 0x88: case 0xD3: case 0xD4:
        case 0xD5: case 0xD6: case 0xD8:
            return true;
        default: return false;
    }
}

// 0x004597FD, case d'ancrage : image du sol non animée (+0x1E de son entrée nul, FUN_00423180),
// et bâtiment d'un autre type que 0x83 et 0x39. `building` reçoit EBX en 0x00459CE2.
bool plain_animation(Cpu& cpu, int32_t tile, uint32_t bits, int32_t& building) {
    const int32_t image = i32(cpu, at(kTileImage, tile, 4));
    const int32_t bank = image / 16384, number = image % 16384;
    const uint32_t banks = at(kSpriteBanks, bank, 0xCC44);
    const uint32_t table = u32(cpu, banks + 0x1000);
    const uint32_t entry = number <= i32(cpu, banks + 0xDC34) ? at(table, number, 0x48) : table;
    if (s16(cpu, entry + 0x1E) != 0) return false;  // FUN_00413380, FUN_0045d140 : image animée
    building = s16(cpu, at(kTileBuilding, tile, 2));
    const int32_t type = building_type(cpu, building);
    return !((type == 0x83 && (bits & 0x40)) || type == 0x39);  // FUN_004e5730, FUN_004574c0
}

// Étape 4 (0x00459553 à 0x00459CE2) : animation du bâtiment. Rien n'est dessiné dans les cas
// traités : pas d'animation (+0x1E de l'image nul) ou case sans ancrage. Renvoie faux pour laisser
// l'étape à l'original ; sinon, `building` et `bits` sont EBX et [ESP+0x28] en 0x00459CE2.
bool objects_animation(Cpu& cpu, uint32_t esp, const ObjectCell& c, int32_t& building, uint32_t& bits) {
    const int32_t tile = c.tile;
    const int32_t first = i32(cpu, esp + kObjectBuilding);
    const int32_t type = building_type(cpu, first);
    if (first != 0 && (u8(cpu, at(kTileBits, tile, 1)) & 0x40)) {
        if (listed_building(cpu, type)) return false;      // FUN_0040abf0
        if (type == 0x31 || type == 0xD5) return false;   // FUN_0044f320 pour ces deux types
    }
    bits = u8(cpu, at(kTileBits, tile, 1));
    if ((bits & 0x40) == 0) {
        // Case sans ancrage : tous les chemins mènent à 0x00459CE2 sans rien dessiner, sauf
        // FUN_004574c0 pour le type 0x39. Les conditions du switch appellent des fonctions qui
        // écrivent (FUN_004faa00 pour 0x7D) ou non relues (FUN_0040abd0) : laissées à l'original.
        building = s16(cpu, at(kTileBuilding, tile, 2));
        if (listed_building(cpu, type) || type == 0x7D || building_type(cpu, building) == 0x39) return false;
    } else {
        if (s16(cpu, at(kBuildings + 0x3A, first, 0x118)) != 0) return false;  // FUN_0045a8e0 et suivantes
        if (listed_building(cpu, type) || special_building(type)) return false;
        if (!plain_animation(cpu, tile, bits, building)) return false;
    }
    if (first != 0 && u8(cpu, at(kBuildings + 0xBF, first, 0x118)) != 0) put16(cpu, kOverlay, 0xF863);
    return true;
}

// Étape 7 (0x00459D7A à 0x00459FF0) : personnages des cases r - 4, r - 2 et r, puis la vie sur
// l'eau. Traitée quand ces trois listes sont vides : il ne reste alors que l'anneau kWalkerRing à
// tenir à jour. Sinon l'original fait l'étape entière (tri FUN_00449b30, FUN_0044fef0).
bool objects_walkers(Cpu& cpu, uint32_t esp, const ObjectCell& c) {
    const uint32_t ring = kWalkerRing + static_cast<uint32_t>(c.column);
    const uint32_t four = ring + u32(cpu, esp + kRingFour) * kColumns, two = ring + u32(cpu, esp + kRingTwo) * kColumns,
                   now = ring + u32(cpu, esp + kRingNow) * kColumns;
    // Cases r - 4 et r - 2, lues sans contrôle de la rangée, comme l'original (0x00459DA7, 0x00459E56).
    const bool deferred = (u8(cpu, four) & 2) != 0;
    if (deferred) {
        const int32_t far = i32(cpu, at(kTileGrid, kColumns * (c.row - 4) + c.column, 4));
        if (far < 0 || far >= kMark || s16(cpu, at(kTileFigure, far, 2)) != 0) return false;
    }
    const int32_t behind = i32(cpu, at(kTileGrid, kColumns * (c.row - 2) + c.column, 4));
    if (behind < kMark && (behind < 0 || s16(cpu, at(kTileFigure, behind, 2)) != 0)) return false;
    if (s16(cpu, at(kTileFigure, c.tile, 2)) != 0) return false;  // FUN_0045a5a0
    // FUN_0041d3d0 : animal ou bateau sur l'eau, partie de Poséidon.
    if (poseidon(cpu) && (u32(cpu, at(kTileFlags, c.tile, 4)) & 0x44) == 0x44 && u8(cpu, at(kTileLife, c.tile, 1)) != 0)
        return false;
    if (deferred) put8(cpu, four, u8(cpu, four) ^ 2);
    if (behind < kMark && (u8(cpu, two) & 1)) put8(cpu, two, u8(cpu, two) ^ 1);
    if (const uint8_t hidden = u8(cpu, at(kTileHidden, c.tile, 1))) put8(cpu, now, u8(cpu, now) | hidden);
    return true;
}

constexpr uint32_t kRestart = 1;  // la case entière revient à l'original

uint32_t objects_cell(Cpu& cpu, uint32_t esp, ObjectCell& c) {
    if (!objects_ground(cpu, esp, c)) return kRestart;
    objects_ambient(cpu, esp, c);
    if (!objects_object(cpu, c)) return kAfterAmbient;
    int32_t building = 0;
    uint32_t bits = 0;
    if (!objects_animation(cpu, esp, c, building, bits)) {
        cpu.set_reg(Cpu::Reg::EBP, static_cast<uint32_t>(c.y));
        return kAfterObject;
    }
    if ((bits & 0x40) && building_extra(building_type(cpu, building))) {
        const int32_t type = building_type(cpu, building);
        if (type != 0x31 && type != 0xD5) {
            put32(cpu, esp + kLift, bits);
            cpu.set_reg(Cpu::Reg::EBX, static_cast<uint32_t>(building));
            cpu.set_reg(Cpu::Reg::EDI, static_cast<uint32_t>(c.x));
            cpu.set_reg(Cpu::Reg::EBP, static_cast<uint32_t>(c.y));
            return kAfterAnimation;
        }
    }
    put16(cpu, kOverlay, 0);
    // 0x00459D23 : méthode +0x0C du mode de vue 0, qui renvoie faux (vérifiée par objects_known).
    if (!objects_walkers(cpu, esp, c)) {
        cpu.set_reg(Cpu::Reg::EDI, static_cast<uint32_t>(c.x));
        cpu.set_reg(Cpu::Reg::EBP, static_cast<uint32_t>(c.y));
        return kWalkers;
    }
    return 0;
}

// Le mode de vue 0 et sa méthode +0x0C « faux » (0x00565750) : seul cas traité.
bool objects_known(Cpu& cpu) {
    if (u32(cpu, kViewMode) != 0) return false;
    const uint32_t view = u32(cpu, kViewModes);
    return u32(cpu, u32(cpu, view) + 0x0C) == kAlwaysFalse;
}

// Aiguillage en 0x00458792 : EDI est la rangée, ECX la colonne (dans la grille).
uint32_t objects_row(Cpu& cpu) {
    if (!objects_known(cpu)) return 0;
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    const int32_t row = static_cast<int32_t>(cpu.reg(Cpu::Reg::EDI));
    for (;;) {
        const int32_t column = i32(cpu, kViewColumn);
        if (column >= 0 && column < kColumns) {
            const int32_t tile = i32(cpu, at(kTileGrid, kColumns * row + column, 4));
            put32(cpu, kViewTile, static_cast<uint32_t>(tile));
            if (tile < kMark) {
                const uint32_t resume = tile < 0 ? kRestart : [&] {
                    ObjectCell c{row, column, tile, i32(cpu, esp + kX), 0, u32(cpu, esp + kOdd)};
                    return objects_cell(cpu, esp, c);
                }();
                if (resume == kRestart) {
                    cpu.set_reg(Cpu::Reg::ECX, static_cast<uint32_t>(column));
                    return 0;
                }
                if (resume != 0) return resume;
            }
        }
        // Case suivante (0x00459FF0).
        put32(cpu, esp + kX, u32(cpu, esp + kX) + u32(cpu, kTileStep) + 2);
        const int32_t counter = i32(cpu, esp + kCounter) + 1;
        put32(cpu, esp + kCounter, static_cast<uint32_t>(counter));
        put32(cpu, kViewColumn, u32(cpu, kViewColumn) + 1);
        if (counter > i32(cpu, kViewWidth) + 8) {
            cpu.set_reg(Cpu::Reg::EDX, u32(cpu, esp + kRingNow));
            cpu.set_reg(Cpu::Reg::ESI, u32(cpu, kHalfTileHeight));
            return kObjectsRowEnd;
        }
    }
}

// Empreinte FNV-1a 64 bits d'une zone de la mémoire du jeu.
uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t a = start; a < end; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

// Empreinte de plusieurs zones mises bout à bout.
uint64_t fingerprint(Cpu& cpu, const uint32_t (*ranges)[2], size_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < count; ++i)
        for (uint32_t a = ranges[i][0]; a < ranges[i][1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

bool ground_code = false;    // FUN_00435070 vérifiée
bool ground_halves = false;  // FUN_004377b0 et FUN_00438a90 vérifiées

}  // namespace

// FUN_00435070 : le losange a 15 lignes qui s'élargissent de 4 pixels (2, 6… 58), centrées dans
// les 58 pixels de l'image, puis 15 qui rétrécissent d'autant ; l'image les range bout à bout,
// soit 450 mots de 32 bits. Rognée en haut (3), seule la moitié basse est dessinée, à partir de
// la première ligne ; rognée en bas (4), seule la moitié haute.
void draw_ground(Cpu& cpu, uint32_t image, uint8_t clip_y) {
    const uint32_t row_bytes = u32(cpu, kRowBytes);
    const uint32_t origin = u32(cpu, kFrameBuffer) + u32(cpu, kTargetX) * 2 + u32(cpu, kTargetY) * row_bytes;
    const uint8_t* source = cpu.host(image);
    uint32_t row = 0;
    if (clip_y != 3) {
        for (uint32_t r = 0; r < 15; ++r, ++row) {
            const uint32_t bytes = (4 * r + 2) * 2;
            std::memcpy(cpu.host(origin + row * row_bytes + (28 - 2 * r) * 2), source, bytes);
            source += bytes;
        }
        if (clip_y == 4) return;
    } else {
        source += 225 * 4;
    }
    for (uint32_t r = 0; r < 15; ++r, ++row) {
        const uint32_t bytes = (58 - 4 * r) * 2;
        std::memcpy(cpu.host(origin + row * row_bytes + 4 * r), source, bytes);
        source += bytes;
    }
}

// FUN_004377b0 (rognée à gauche) et FUN_00438a90 (à droite) : le losange n'est pas rogné au pixel
// près, une moitié fixe est dessinée. À gauche, les colonnes 30 à 57 de l'image, écrites à partir
// de kTargetX (comme si 30 pixels exactement étaient masqués) ; à droite, les colonnes 0 à 27 à
// leur place. Les deux routines sont 210 copies écrites une à une ; deux s'écartent de cette
// règle, à gauche : sur la ligne 15, les colonnes 38 et 39 reçoivent les pixels 36 et 37 (le mot
// en +0x3CC lu deux fois, 0x00437C48) ; sur la ligne 25, la colonne 37 reçoit le pixel 35 (moitié
// haute d'EAX restée du mot précédent après « MOV AX », 0x00437FFE). Rognage vertical comme
// draw_ground.
void draw_ground_half(Cpu& cpu, uint32_t image, uint8_t clip_y, bool left) {
    const uint32_t row_bytes = u32(cpu, kRowBytes);
    const uint32_t origin = u32(cpu, kFrameBuffer) + u32(cpu, kTargetX) * 2 + u32(cpu, kTargetY) * row_bytes;
    uint32_t start = 0;  // octet de la ligne dans l'image
    for (uint32_t r = 0; r < 30; ++r) {
        const uint32_t width = r < 15 ? 4 * r + 2 : 58 - 4 * (r - 15), first = (58 - width) / 2;
        const bool drawn = r < 15 ? clip_y != 3 : clip_y != 4;
        const uint32_t row = clip_y == 3 ? r - 15 : r;
        if (drawn) {
            for (uint32_t column = first; column < first + width; ++column) {
                if (left ? column < 30 : column >= 28) continue;
                uint32_t from = column;
                if (left && ((r == 15 && (column == 38 || column == 39)) || (r == 25 && column == 37))) from -= 2;
                std::memcpy(cpu.host(origin + row * row_bytes + (left ? column - 30 : column) * 2),
                            cpu.host(image + start + (from - first) * 2), 2);
            }
        }
        start += width * 2;
    }
}

bool ground_known() { return ground_code; }
bool ground_halves_known() { return ground_halves; }

int install_native_drawing(Process& p) {
    Cpu& cpu = p.cpu();
    ground_code = fingerprint(cpu, kDrawGround, kDrawGroundEnd) == 0x00E2CC5E0BBFF6E0ull;
    ground_halves = fingerprint(cpu, kGroundLeft, kGroundLeftEnd) == 0xDDD1B557B7C45DF4ull &&
                    fingerprint(cpu, kGroundRight, kGroundRightEnd) == 0x9305F17B50FA7CE8ull;
    if (ground_halves) {
        // « PUSH EBP ; MOV EBP,ESP ; CMP word ptr [surimpression],0 » déplacés ; EAX est rendu tel quel.
        for (const bool left : {true, false})
            cpu.add_fast_path(left ? kGroundLeft : kGroundRight, 0, 11, [left](Cpu& c) -> std::optional<uint32_t> {
                if (u16(c, kOverlay) != 0) return std::nullopt;  // FUN_00438050, FUN_00439340
                draw_ground_half(c, c.arg(0), static_cast<uint8_t>(c.arg(1)), left);
                return c.reg(Cpu::Reg::EAX);
            });
    }
    // Code réécrit : la boucle du sol, FUN_00457900 et ses tables de sauts, la table de sauts du
    // type de bâtiment de la boucle, et la méthode « vrai » du mode de vue 0.
    if (fingerprint(cpu, kGroundLoop, kGroundRowEnd) != 0xF739BF1DE60A7A90ull ||
        fingerprint(cpu, kGroundTile, kGroundTileEnd) != 0xCEEC3987CB62C910ull ||
        fingerprint(cpu, kBuildingJumps, kBuildingJumpsEnd) != 0xC3AF07CCE1702E33ull ||
        !matches(cpu, kAlwaysTrue, {0xB0, 0x01, 0xC2, 0x04, 0x00}))
        return 0;
    cpu.add_fast_path(kGroundTile, 0, 5, ground_tile_call);  // « SUB ESP,8 ; PUSH EBX ; PUSH EBP » déplacés
    cpu.add_code_jump(kGroundLoop, 7, ground_row);           // « MOV EAX,[colonne] ; TEST EAX,EAX » déplacés
    // Second parcours : voir kObjectsCode et kObjectHelpers (game_gog.h). La table de FUN_0040aa20
    // et les fiches lues sont des données.
    if (fingerprint(cpu, kObjectsCode, kObjectsCodeEnd) != kObjectsCodeHash ||
        fingerprint(cpu, kBuildingExtras, kBuildingExtrasEnd) != kBuildingExtrasHash ||
        fingerprint(cpu, kSlopedTop, kSlopedTopEnd) != kSlopedTopHash ||
        fingerprint(cpu, kObjectHelpers, std::size(kObjectHelpers)) != kObjectHelpersHash ||
        !matches(cpu, kAlwaysFalse, {0x32, 0xC0, 0xC2, 0x08, 0x00}) ||
        !matches(cpu, kViewModeZeroSetup, {0xC7, 0x05, 0xFC, 0x68, 0xFE, 0x00, 0x24, 0x44, 0x5E, 0x00}))
        return 2;
    cpu.add_code_jump(kObjectsCell, 6, objects_row);  // « LEA EAX,[EDI+EDI*8] ; LEA EAX,[EDI+EAX*2] » déplacés
    return 3;
}

}  // namespace zeus::win32
