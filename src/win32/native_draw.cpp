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
int32_t s8(Cpu& cpu, uint32_t address) { return static_cast<int8_t>(*cpu.host(address)); }
uint8_t u8(Cpu& cpu, uint32_t address) { return *cpu.host(address); }
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }
void put16(Cpu& cpu, uint32_t address, uint16_t value) { std::memcpy(cpu.host(address), &value, 2); }

uint32_t at(uint32_t base, int32_t index, uint32_t size) { return base + static_cast<uint32_t>(index) * size; }

constexpr int32_t kColumns = 0xE7;      // cases par rangée de la grille
constexpr int32_t kMark = 0x0FFF0000;   // à partir de là, la grille contient une marque, pas une case

// Premier identifiant d'image du groupe `group` de l'interface du terrain, ou du groupe 0 quand
// il n'existe pas (FUN_00457900 : « DAT_012fc574 < n ? groupe 0 : groupe n »).
int32_t terrain_group(Cpu& cpu, int32_t group) {
    const uint32_t groups = u32(cpu, kTerrainGroups);
    return i32(cpu, (i32(cpu, kTerrainGroupCount) < group ? groups : at(groups, group, 8)) + 4);
}

// FUN_00457900, fin : un personnage de certains types sur l'une des deux cases de la rangée
// précédente qui touchent celle-ci (selon la parité de la rangée) se dessine par FUN_005adf00.
bool walker_behind(Cpu& cpu, uint32_t odd) {
    const uint32_t cell = at(kTileGrid, kColumns * i32(cpu, kViewRow) + i32(cpu, kViewColumn), 4);
    const int32_t first = i32(cpu, cell - (odd ? 0x3A0 : 0x39C)), second = i32(cpu, cell - (odd ? 0x39C : 0x398));
    for (const int32_t tile : {first, second}) {
        if (tile >= kMark || u8(cpu, at(kTileHidden, tile, 1)) != 0) continue;
        for (int32_t figure = s16(cpu, at(kTileFigure, tile, 2)); figure > 0;
             figure = s16(cpu, at(kFigures, figure, 0x184))) {
            if (u8(cpu, at(kFigures + 0x5D, figure, 0x184)) != 0) continue;
            switch (u8(cpu, at(kFigures + 2, figure, 0x184))) {
                case 0x01: case 0x04: case 0x0D: case 0x1C: case 0x2A: case 0x43: case 0x44: case 0x45:
                    return true;
                default: break;
            }
        }
    }
    return false;
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
    const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
    if (building != 0) return false;  // FUN_0045dc10, surimpression rouge
    if (i32(cpu, kHighlight) == 1) return false;  // FUN_0051ef30
    const uint8_t slope = u8(cpu, at(kTileSlope, tile, 1));
    if (slope & 0x0F) return false;
    int32_t image = i32(cpu, at(kTileImage, tile, 4));
    if (slope & 0x10) image = terrain_group(cpu, 12) + 0x14000;
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
    if (flags & 0x01004028) {  // FUN_00507980, FUN_005ae9f0, FUN_0044b050 (sauf 8 en mode 0)
        if ((flags & 0x01004020) || mode != 0) return false;
    }
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

// Empreinte FNV-1a 64 bits d'une zone de la mémoire du jeu.
uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t a = start; a < end; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

bool ground_code = false;  // FUN_00435070 vérifiée

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

bool ground_known() { return ground_code; }

int install_native_drawing(Process& p) {
    Cpu& cpu = p.cpu();
    ground_code = fingerprint(cpu, kDrawGround, kDrawGroundEnd) == 0x00E2CC5E0BBFF6E0ull;
    // Code réécrit : la boucle du sol, FUN_00457900 et ses tables de sauts, la table de sauts du
    // type de bâtiment de la boucle, et la méthode « vrai » du mode de vue 0.
    if (fingerprint(cpu, kGroundLoop, kGroundRowEnd) != 0xF739BF1DE60A7A90ull ||
        fingerprint(cpu, kGroundTile, kGroundTileEnd) != 0xCEEC3987CB62C910ull ||
        fingerprint(cpu, kBuildingJumps, kBuildingJumpsEnd) != 0xC3AF07CCE1702E33ull ||
        !matches(cpu, kAlwaysTrue, {0xB0, 0x01, 0xC2, 0x04, 0x00}))
        return 0;
    cpu.add_fast_path(kGroundTile, 0, 5, ground_tile_call);  // « SUB ESP,8 ; PUSH EBX ; PUSH EBP » déplacés
    cpu.add_code_jump(kGroundLoop, 7, ground_row);           // « MOV EAX,[colonne] ; TEST EAX,EAX » déplacés
    return 2;
}

}  // namespace zeus::win32
