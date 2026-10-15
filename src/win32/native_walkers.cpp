// Personnages de la ville réécrits pour l'hôte : FUN_0044fef0, qui dessine un personnage, et
// FUN_0041d3d0, l'animal ou le bateau sur l'eau d'une case.
//
// Le second parcours de la boucle de dessin (native_draw.cpp) rend la main à l'original pour toute
// case où se tient un personnage : l'original trie les personnages de la case (FUN_00449b30) puis
// appelle FUN_0044fef0 pour chacun. Dans la grande ville (`working-city`, 300 s), FUN_0044fef0 est
// appelée 27 178 fois, dont 13 711 pour un personnage à dessiner (--count 0x0044FF29) ; les autres
// ne sont pas du plan dessiné (octet +0x15 de la fiche contre le dernier argument).
//
// Les deux fonctions ont un chemin rapide (Cpu::add_fast_path). Celui de FUN_0044fef0 décide dès
// l'entrée, sans rien écrire, si l'appel est un cas traité : tous les sprites à dessiner sont des
// cas du répartiteur natif (sprite_handled), aucune fonction du jeu non réécrite n'est appelée
// (objets C++ des sanctuaires, texte des marchands) ; sinon l'original fait
// tout. Les variables sont ensuite écrites dans l'ordre de l'original : rien de ce qu'il lit
// après une écriture ne dépend de celle-ci.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si ses octets sont ceux attendus
// (empreinte FNV-1a, comparée avant toute modification).

#include <cstddef>
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
int32_t s8(Cpu& cpu, uint32_t address) { return static_cast<int8_t>(*cpu.host(address)); }
uint8_t u8(Cpu& cpu, uint32_t address) { return *cpu.host(address); }
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }

uint32_t at(uint32_t base, int32_t index, uint32_t size) { return base + static_cast<uint32_t>(index) * size; }

constexpr int32_t kColumns = 0xE7;     // cases par rangée de la grille
constexpr int32_t kGridRows = 0x1C9;   // rangées de la grille
constexpr int32_t kMark = 0x0FFF0000;  // à partir de là, la grille contient une marque, pas une case

// Champ d'une fiche de personnage (0x184 octets), à son adresse absolue pour la fiche 0.
uint32_t field(uint32_t address, int32_t figure) { return at(address, figure, 0x184); }

// FUN_004200f0(0) : vrai quand ces deux variables valent 1 (partie de Poséidon, hypothèse).
bool poseidon(Cpu& cpu) { return u32(cpu, kGameMode) == 1 && s16(cpu, kGameSubmode) == 1; }

// FUN_0041d3d0(case, x, y, …) ne fait rien (premiers tests, 0x0041D3E5 à 0x0041D40D) hors d'une
// partie de Poséidon, ou si la case n'est pas de l'eau (0x44) où vit un animal ou un bateau.
bool water_life(Cpu& cpu, int32_t tile) {
    return poseidon(cpu) && (u32(cpu, at(kTileFlags, tile, 4)) & 0x44) == 0x44 && u8(cpu, at(kTileLife, tile, 1)) != 0;
}

// Entrée de la table d'images d'un identifiant (banque * 16384 + numéro, divisions entières
// signées) ; au-delà du dernier numéro, la première de la banque (0x00450310 et suivantes).
uint32_t image_entry(Cpu& cpu, int32_t id) {
    const uint32_t bank = kSpriteBanks + 0x1000 + static_cast<uint32_t>(id / 16384) * 0xCC44;
    const int32_t number = id % 16384;
    return i32(cpu, bank + 0xCC34) < number ? u32(cpu, bank) : u32(cpu, bank) + static_cast<uint32_t>(number) * 0x48;
}

// Premier identifiant d'image du groupe `group` d'une table de groupes (8 octets par groupe, le
// premier identifiant en +4), ou du groupe 0 quand la table en compte moins (0x00450A3F et suivantes).
int32_t group_image(Cpu& cpu, uint32_t groups, uint32_t count, int32_t group) {
    const uint32_t table = u32(cpu, groups);
    return i32(cpu, (i32(cpu, count) < group ? table : at(table, group, 8)) + 4);
}

// FUN_004d7210(personnage) : le personnage est un animal ou un bateau sur l'eau.
bool on_water(Cpu& cpu, int32_t figure) {
    switch (u8(cpu, field(kFigureType, figure))) {
        case 0x14: case 0x19: case 0x23: case 0x2C: case 0x39: case 0x3F: case 0x40:
            return true;
        case 0x06: return u8(cpu, field(kFigureKind, figure)) == 0x0D;
        case 0x43: return u8(cpu, field(kFigureState, figure)) == 1;
        case 0x44: {
            const uint8_t state = u8(cpu, field(kFigureState, figure));
            return state == 1 || state == 0x0A;
        }
        case 0x45: {
            const int32_t tile = i32(cpu, field(kFigureTile, figure));
            const uint32_t flags = u32(cpu, at(kTileFlags, tile, 4));
            return (flags & 0x4000000) != 0 && !((flags & 0x44) == 0x44 && u8(cpu, at(kTileLife, tile, 1)) != 0);
        }
        default: return false;
    }
}

// Un sprite passé au répartiteur : FUN_005acf50(identifiant, x, y, mode, &couleur, miroir).
struct Sprite {
    int32_t id, x, y, mode;
    uint8_t mirror;
};

// Ce que dessine FUN_0044fef0, calculé sans rien écrire.
struct WalkerPlan {
    int32_t direction;  // écrite dans kFigureViewDirection
    bool stop;          // kWalkersOff : l'original revient juste après cette écriture
    bool matched;       // le personnage est celui cherché : sa position va dans kFoundFigure
    int32_t found_x, found_y;
    Sprite sprites[3];
    int count;
    bool tail;  // FUN_0041d3d0 et cases voisines après les sprites (0x00451137)
};

// Bits 0x40000000 des cases voisines plus hautes (0x00451159 à 0x00451641) : la case est à
// redessiner par-dessus le personnage. `apply` faux : ne fait que vérifier qu'aucune n'appelle
// FUN_00546530 (objet C++ d'un sanctuaire) et que les images de sol sautées sont prêtes.
bool mark_neighbours(Cpu& cpu, int32_t figure, bool apply) {
    bool tall = false;  // 9 voisines au lieu de 4, et les 4 premières, pas plus hautes, perdent le bit
    int32_t last = 3;
    switch (u8(cpu, field(kFigureType, figure))) {
        case 0x01: case 0x04: case 0x0D: case 0x1C: case 0x2A: case 0x43: case 0x44: case 0x45:
            tall = true;
            last = 8;
            break;
        default: break;
    }
    const int32_t row = i32(cpu, kViewRow), column = i32(cpu, kViewColumn);
    const uint32_t offsets = ((row - i32(cpu, kViewFirstRow)) & 1) ? kNeighboursOdd : kNeighboursEven;
    const int32_t height = s8(cpu, at(kTileHeight, i32(cpu, field(kFigureTile, figure)), 1));
    for (int32_t n = 0; n <= last; ++n) {
        const int32_t r = row + i32(cpu, offsets + 8 * n), c = column + i32(cpu, offsets + 8 * n + 4);
        if (c < 0 || c >= kColumns || r < 0 || r >= kGridRows) continue;
        const int32_t tile = i32(cpu, at(kTileGrid, kColumns * r + c, 4));
        if (tile >= kMark) continue;
        const uint32_t flags = at(kTileFlags, tile, 4);
        if (s8(cpu, at(kTileHeight, tile, 1)) <= height) {
            if (apply && n <= 3 && tall) put32(cpu, flags, u32(cpu, flags) & 0xBFFFFFFF);
            continue;
        }
        const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
        if (building != 0 && (u8(cpu, at(kTileBits, i32(cpu, kViewTile), 1)) & 0x40)) {
            const int32_t type = s16(cpu, at(kBuildings, building, 0x118));
            if (type == 2 || type == 9 || type == 0x7B || type == 0x86) continue;
            // FUN_005344a0 : sanctuaires (0x54 à 0x62, 0x75) ; monuments (100 à 0x72).
            if ((type >= 0x54 && type <= 0x62) || type == 0x75 || (type >= 100 && type <= 0x72)) return false;
        }
        if (u32(cpu, kViewMode) != 0x11) {
            if (u8(cpu, kSkipReady) == 0) return false;  // préparation des images sautées (0x00451302)
            const uint32_t ground = u32(cpu, at(kTileImage, tile, 4)), first = u32(cpu, kSkipFirst);
            if (ground >= first && ground <= u32(cpu, kSkipLast)) {
                const uint32_t bit = ground - first;
                if (u32(cpu, kSkipBits + (bit >> 5) * 4) & (1u << (bit & 31))) continue;
            }
        }
        if (apply) put32(cpu, flags, u32(cpu, flags) | 0x40000000);
    }
    return true;
}

// Méthode +0x14 du mode de vue courant, appelée avec le personnage : seule « faux » (0x005656F0,
// celle du mode 0) est traitée ; « vrai » ferait appeler la méthode +4.
bool view_ignores(Cpu& cpu) {
    const uint32_t view = u32(cpu, at(kViewModes, i32(cpu, kViewMode), 4));
    return u32(cpu, u32(cpu, view) + 0x14) == kViewFalse;
}

// FUN_0044fef0(personnage, x, y, cherché, plans), jusqu'aux sprites : renvoie faux pour un cas
// non traité. `cherché` : 0, un personnage dont on veut la position, ou 9999 (FUN_0044fea0).
bool plan_walker(Cpu& cpu, int32_t figure, int32_t x, int32_t y, uint32_t wanted, WalkerPlan& p) {
    const uint8_t type = u8(cpu, field(kFigureType, figure));
    const uint8_t kind = u8(cpu, field(kFigureKind, figure));
    const int32_t colour = i32(cpu, field(kFigureColour, figure));
    const int32_t mode = colour != 0;
    const int32_t body = i32(cpu, field(kFigureImage, figure)), extra = i32(cpu, field(kFigureExtraImage, figure));
    const int32_t tile = i32(cpu, field(kFigureTile, figure));

    // Décalage du sprite dans la case (0x0044FF29 à 0x00450069).
    int32_t dx = 2, dy = 10;
    switch (type) {
        case 0x01: case 0x0B: case 0x0C: case 0x0D: case 0x28: case 0x29: case 0x2A: case 0x37: case 0x3B: {
            // Bateau ou personnage sur un quai : la case du bas (bit 0x40 de sa nature) contre l'octet +0x39.
            const bool low = (u32(cpu, at(kTileFlags, i32(cpu, field(kFigureDock, figure)), 4)) >> 6) & 1;
            if (low == (u8(cpu, field(kFigureDocked, figure)) != 0)) {
                dy = (low ? 0x0C : 0) - 5;
            } else {
                static constexpr int32_t kRise[16] = {1, 2, 3, 3, 4, 5, 6, 7, -5, -5, -4, -3, -2, -1, -1, 0};
                uint32_t step = u8(cpu, field(kFigureStep, figure)) & 0x0F;
                if (low) step = 15 - step;
                dy = kRise[step];
            }
            dy += 10;
            break;
        }
        case 0x06:
            if (kind == 0x12 || kind == 0x0D) return false;  // FUN_00534a70
            break;
        case 0x3A: return false;  // FUN_00534a70
        default: break;
    }
    if (u32(cpu, at(kTileFlags, tile, 4)) & 0x20000) {
        const uint8_t depth = u8(cpu, at(kTileDepth, tile, 1));
        if (depth == 0) dy += 8;
        else if (depth == 0x64) dy += 4;
    }
    int32_t direction = s8(cpu, field(kFigureDirection, figure));
    if (direction >= 8) {
        direction = s8(cpu, field(kFigureDirection + 1, figure));
        if (direction >= 8) direction = 0;
    }
    direction -= i32(cpu, kViewOrientation);
    if (direction < 0) direction += 8;
    p.direction = direction;
    p.stop = u32(cpu, kWalkersOff) == 1;
    p.count = 0;
    if (p.stop) return true;

    const int32_t orientation = i32(cpu, kViewOrientation);
    if (u8(cpu, field(kFigureMoving, figure)) == 0) {
        const int32_t index = direction * 16 + s8(cpu, field(kFigureStep, figure));
        dx = s8(cpu, at(kStepX, index, 1)) + 2;
        dy += s8(cpu, at(kStepY, index, 1)) - u8(cpu, field(kFigureLift, figure));
    } else {
        const int32_t index = s16(cpu, field(kFigureFineX, figure)) % 15 + s16(cpu, field(kFigureFineY, figure)) % 15 * 16;
        if (orientation == 0) {
            dx = s8(cpu, at(kFineX, index, 1)) + 2;
            dy += s8(cpu, at(kFineY, index, 1));
        } else if (orientation == 2) {
            dx = s8(cpu, at(kFineXTurned, index, 1)) + 2;
            dy -= s8(cpu, at(kFineYTurned, index, 1));
        } else if (orientation == 4) {
            dx = 2 - s8(cpu, at(kFineX, index, 1));
            dy -= s8(cpu, at(kFineY, index, 1));
        } else if (orientation == 6) {
            dx = 2 - s8(cpu, at(kFineXTurned, index, 1));
            dy += s8(cpu, at(kFineYTurned, index, 1));
        }
        dy -= u8(cpu, field(kFigureAltitude, figure));
        const int32_t link = s16(cpu, field(kFigureLink, figure));
        if (type == 0x2D && u8(cpu, field(kFigureType, link)) == 0x2E) {
            const int32_t lift = u8(cpu, field(kFigureLift, figure));
            const int32_t building = s16(cpu, field(kFigureLink, link));
            if (poseidon(cpu) && (u8(cpu, at(kBuildings + 0xB4, building, 0x118)) & 0x20)) dy += 0x13 - lift;
            else dy -= lift;
        }
        if (type == 0x06) {
            dx += s8(cpu, field(kFigureExtraX, figure));
            dy += s8(cpu, field(kFigureExtraY, figure)) - u8(cpu, field(kFigureLift, figure));
            if (kind == 0x0C) {
                // FUN_00411870 sur la case du personnage et sur celle du bâtiment (+-4 de sa fiche).
                const int32_t turn = i32(cpu, at(kTurnOffsets, orientation / 2, 4));
                const int32_t here = tile + turn;
                const int32_t home = i32(cpu, at(kBuildings - 4, s16(cpu, field(kFigureBuilding, figure)), 0x118)) + turn;
                dy += (s8(cpu, at(kTileHeight, here, 1)) - s8(cpu, at(kTileHeight, home, 1))) * 30;
            }
        }
    }
    const int32_t base_y = dy + i32(cpu, kHalfTileHeight) - 10, base_x = dx + i32(cpu, kHalfTileWidth) - 2;
    const int32_t body_x = dx + i32(cpu, kHalfTileWidth) - s16(cpu, image_entry(cpu, body) + 0x22);
    const int32_t body_y = dy + i32(cpu, kHalfTileHeight) - s16(cpu, image_entry(cpu, body) + 0x24);
    const int32_t extra_x = base_x - s16(cpu, image_entry(cpu, extra) + 0x22);
    const int32_t extra_y = base_y - s16(cpu, image_entry(cpu, extra) + 0x24);
    p.matched = false;
    if (wanted == 9999) return false;  // FUN_0044fea0
    if (wanted != 0) {
        if (static_cast<uint32_t>(figure) != wanted) {
            p.stop = true;
            return true;
        }
        p.matched = true;
        p.found_x = body_x + x;
        p.found_y = body_y + y;
    }
    const int32_t third = i32(cpu, field(kFigureThirdImage, figure));
    const int32_t third_x = base_x - s16(cpu, image_entry(cpu, third) + 0x22) + 1;
    const int32_t third_y = base_y - s16(cpu, image_entry(cpu, third) + 0x24);
    const int8_t shift_x = static_cast<int8_t>(s8(cpu, field(kFigureExtraX, figure)));
    const int8_t shift_y = static_cast<int8_t>(s8(cpu, field(kFigureExtraY, figure)));
    p.tail = true;
    auto add = [&p, mode](int32_t id, int32_t sx, int32_t sy, uint8_t mirror = 0, bool tinted = true) {
        p.sprites[p.count++] = {id, sx, sy, tinted ? mode : 0, mirror};
    };
    const int32_t bx = body_x + x, by = body_y + y;
    if (extra == 0) {
        // 0x00450541 : le corps seul.
        if (type == 0x06) {
            const uint8_t mirror = kind == 0x0E ? static_cast<uint8_t>((orientation / 2) & 1)  // FUN_00411860
                                                : static_cast<uint8_t>((u8(cpu, field(kFigureFlags, figure)) >> 6) & 1);
            add(body, bx, by, mirror);
        } else {
            add(body, bx, by);
            if (type == 0x39 && third != 0) add(third, third_x + x, third_y + y);
        }
        // FUN_004d7210 vrai : FUN_0041d3d0 sur la case et sept voisines (table 0x005D9188).
        if (on_water(cpu, figure)) {
            if (water_life(cpu, tile)) return false;
            for (int32_t n = 0; n < 7; ++n)
                if (water_life(cpu, tile + i32(cpu, kWaterNeighbours + 4 * n))) return false;
        }
        // Le bateau de Poséidon (bâtiment 199) : FUN_0041d150, jamais appelée dans les scénarios.
        if (type == 0x2B && s16(cpu, at(kBuildings, s16(cpu, field(kFigureLink, figure)), 0x118)) == 199) return false;
        if (!view_ignores(cpu)) return false;
    } else {
        const int32_t ex = shift_x + extra_x + x, ey = shift_y + extra_y + y;  // second sprite à sa place
        switch (type) {
            case 0x04: case 0x24: case 0x09: case 0x1C: case 0x43: case 0x45: {
                // 0x00450F2D : l'ordre des deux sprites dépend de la direction dans la vue.
                int32_t facing = s8(cpu, field(kFigureDirection, figure)) + orientation;
                if (facing >= 8) facing -= 8;
                const bool behind = type == 0x43 || (type != 0x45 && (static_cast<uint32_t>(facing) > 7 || facing == 3));
                if (behind) {
                    add(body, bx, by);
                    add(extra, ex, ey);
                } else {
                    add(extra, ex, ey);
                    add(body, bx, by);
                }
                if (!view_ignores(cpu)) return false;
                break;
            }
            case 0x0D: case 0x2A: case 0x01: case 0x02:
                // 0x00450E4D : le second sprite est placé depuis le corps, devant ou derrière.
                if (shift_y < 0) {
                    add(extra, shift_x + bx, shift_y + by);
                    add(body, bx, by);
                } else {
                    add(body, bx, by);
                    add(extra, shift_x + bx, shift_y + by);
                }
                break;
            case 0x0E: {
                // 0x0045089A : fiche de 0x9C octets désignée par le personnage ; rien à dessiner
                // (ni la suite) si son champ +0x7E est positif. Sinon le corps 6 pixels plus haut, le
                // second sprite à droite et au-dessus, puis un troisième selon la sorte de la fiche,
                // tous sans teinte.
                const int32_t cart = s16(cpu, field(kFigureCart, figure));
                if (s16(cpu, at(kCarts + 0x7E, cart, 0x9C)) >= 0) {
                    p.tail = false;
                    break;
                }
                add(body, bx, by - 6, 0, false);
                const int32_t height = s16(cpu, image_entry(cpu, extra) + 0x16);
                add(extra, bx + 9, by - height, 0, false);
                const bool sea = poseidon(cpu);
                int32_t load = 0;
                switch (s16(cpu, at(kCarts, cart, 0x9C))) {
                    case 0x0B:
                        if (cart == 0x1F || cart == 0x20) break;
                        load = sea ? group_image(cpu, kImageGroups52, kImageGroups52Count, 7) + 0xD0001
                                   : group_image(cpu, kImageGroups4, kImageGroups4Count, 0x4B) + 0x10000;
                        break;
                    case 0x0C:
                        load = sea ? group_image(cpu, kImageGroups52, kImageGroups52Count, 7) + 0xD0002
                                   : group_image(cpu, kImageGroups4, kImageGroups4Count, 0x4E) + 0x10000;
                        break;
                    case 0x0D:
                        load = sea ? group_image(cpu, kImageGroups52, kImageGroups52Count, 7) + 0xD0000
                                   : group_image(cpu, kImageGroups4, kImageGroups4Count, 0x4D) + 0x10000;
                        break;
                    default: break;
                }
                if (load != 0)
                    add(load, bx - (sea ? 0 : 1), by - height - s16(cpu, image_entry(cpu, load) + 0x16) + 6, 0, false);
                break;
            }
            case 0x22: return false;  // marchand : texte par FUN_00508f00
            case 0x36: case 0x2D: case 0x34:
                add(extra, bx, by + 10);
                add(body, bx, by);
                break;
            case 0x39:
                add(body, bx, by);
                add(extra, ex + 1, ey + 0x0E);
                if (third != 0) add(third, third_x + x, third_y + y);
                break;
            default: add(body, bx, by); break;
        }
    }
    for (int i = 0; i < p.count; ++i)
        if (!sprite_handled(cpu, kSpriteBanks, p.sprites[i].id, p.sprites[i].x, p.sprites[i].y, p.sprites[i].mode,
                            p.sprites[i].mirror))
            return false;
    if (!p.tail) return true;
    if (type != 0x06 && water_life(cpu, tile)) return false;  // 0x00451137
    return mark_neighbours(cpu, figure, false);
}

std::optional<uint32_t> walker_call(Cpu& cpu) {
    const int32_t figure = static_cast<int32_t>(cpu.arg(0));
    // Plan dessiné (0x0044FF06) : octet +0x15 de la fiche nul, bit 1 du dernier argument, sinon bit 2.
    const uint32_t layers = cpu.arg(4);
    if ((layers & (u8(cpu, field(kFigureLayer, figure)) != 0 ? 2 : 1)) == 0) return cpu.reg(Cpu::Reg::EAX);
    WalkerPlan p;
    const int32_t x = static_cast<int32_t>(cpu.arg(1)), y = static_cast<int32_t>(cpu.arg(2));
    if (!plan_walker(cpu, figure, x, y, cpu.arg(3), p)) return std::nullopt;
    put32(cpu, kFigureViewDirection, static_cast<uint32_t>(p.direction));
    if (p.stop) return cpu.reg(Cpu::Reg::EAX);
    if (p.matched) {
        put32(cpu, kFoundFigureY, static_cast<uint32_t>(p.found_y));
        put32(cpu, kFoundFigureX, static_cast<uint32_t>(p.found_x));
    }
    for (int i = 0; i < p.count; ++i)
        draw_sprite(cpu, kSpriteBanks, p.sprites[i].id, p.sprites[i].x, p.sprites[i].y, p.sprites[i].mode,
                    p.sprites[i].mirror);
    if (p.tail) mark_neighbours(cpu, figure, true);
    return cpu.reg(Cpu::Reg::EAX);
}

// FUN_0041d3d0 (thiscall, 4 arguments) : seuls les appels sans effet sont traités.
std::optional<uint32_t> water_life_call(Cpu& cpu) {
    if (water_life(cpu, static_cast<int32_t>(cpu.arg(0)))) return std::nullopt;
    return cpu.reg(Cpu::Reg::EAX);
}

// Empreinte FNV-1a 64 bits de zones de la mémoire du jeu mises bout à bout.
uint64_t fingerprint(Cpu& cpu, const uint32_t (*ranges)[2], size_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < count; ++i)
        for (uint32_t a = ranges[i][0]; a < ranges[i][1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}
uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    const uint32_t range[1][2] = {{start, end}};
    return fingerprint(cpu, range, 1);
}

}  // namespace

int install_native_walkers(Process& p) {
    Cpu& cpu = p.cpu();
    // Code réécrit : kWalkerHelpers (dont les premiers tests de FUN_0041d3d0), puis FUN_0044fef0
    // et ses tables, FUN_004d7210 et ses tables, la méthode « faux » du mode de vue 0.
    if (fingerprint(cpu, kWalkerHelpers, std::size(kWalkerHelpers)) != kWalkerHelpersHash) return 0;
    cpu.add_fast_path(kWaterLife, 0x10, 6, water_life_call);  // « SUB ESP,0xC0 » déplacé
    int installed = 1;
    if (fingerprint(cpu, kWalker, kWalkerEnd) == kWalkerHash && fingerprint(cpu, kOnWater, kOnWaterEnd) == kOnWaterHash &&
        matches(cpu, kViewFalse, {0x32, 0xC0, 0xC2, 0x04, 0x00})) {
        cpu.add_fast_path(kWalker, 0, 7, walker_call);  // « MOV ECX,[ESP+4] ; SUB ESP,0x64 » déplacés
        ++installed;
    }
    return installed;
}

}  // namespace zeus::win32
