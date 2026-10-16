// Aides des bâtiments composés réécrites pour l'hôte (docs/engine/simulation.md, § 5 et 8).
//
// Sanctuaires, monuments et pièces de pyramide sont faits de pièces de 0x48 octets : décalage
// (x +0, y +4), classe (+8), taille (+0xC). Chaque classe est un objet statique (tables 0x00640E08
// et 0x00641098) dont la méthode 0 renvoie la taille de la pièce et la méthode +0xC0 dit si la
// pièce peut servir d'entrée. Ces aides ne lisent que ces tables, la fiche du bâtiment et les
// grilles de la carte ; elles n'écrivent que leurs résultats, et 0 en +4 de chaque objet de classe
// consulté. Dans la grande ville, elles prennent 42 % du temps propre d'un tour de simulation.
//
// Les petites aides sont remplacées entièrement (Cpu::replace_function). Les trois grandes
// (FUN_0053cc30, FUN_0053cd80, FUN_0053f810) appellent les méthodes des classes : elles passent
// par un chemin rapide (Cpu::add_fast_path) qui lit d'abord le code de chaque méthode atteinte.
// Toutes sont aujourd'hui de la forme « MOV EAX,n ; RET » (taille) ou « XOR AL,AL ; RET » et
// « MOV AL,1 ; RET » (entrée) ; une autre forme, ou une classe absente, rend la main au code
// d'origine avant toute écriture.
//
// En plus, deux morceaux du cycle qui ne font que lire ces tables ou effacer : le parcours des
// bâtiments de multi_tile_buildings_update et un effacement de buildings_spawn_walkers (voir
// multi_tile_update et spawn_clear) ; et les chantiers des bâtiments composés, leur avancement
// et la recherche d'une partie à servir, quand aucune ne l'est (site_progress, site_search).
//
// Les valeurs de retour reproduisent tout EAX, octets hauts compris : plusieurs fonctions
// renvoient un booléen dans AL en laissant dans le reste de EAX ce qui s'y trouvait.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si tous les octets réécrits ici sont
// ceux attendus (empreinte FNV-1a, comparée avant toute modification).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

bool composites_installed = false;  // install_native_buildings a réussi

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
uint8_t u8(Cpu& cpu, uint32_t address) { return *cpu.host(address); }
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }
void put_i32(Cpu& cpu, uint32_t address, int32_t value) { put32(cpu, address, static_cast<uint32_t>(value)); }

uint32_t at(uint32_t base, int32_t index, uint32_t size) { return base + static_cast<uint32_t>(index) * size; }

// AL remplacé par un booléen, le reste de EAX gardé.
uint32_t with_al(uint32_t eax, bool value) { return (eax & 0xFFFFFF00u) | (value ? 1u : 0u); }

constexpr int32_t kMapColumns = 228;  // cases par rangée des grilles de la carte (0xE4)
constexpr uint32_t kPartSize = 0x48;  // une pièce

// FUN_005344a0(type) : vrai pour les types 0x54 à 0x62 et 0x75 (AL ; EAX garde le type).
uint32_t type_composite(int32_t type) {
    const bool composite = (type >= 0x54 && type <= 0x62) || type == 0x75;
    return with_al(static_cast<uint32_t>(type), composite);
}

// FUN_0053c1b0(numéro) sans son écriture : objet de la classe d'une pièce, ou 0.
uint32_t class_object(Cpu& cpu, int32_t number) {
    if (number >= 1000) {
        number -= 1000;
        return number < 7 ? u32(cpu, at(kCompositeClassesHigh, number, 4)) : 0u;
    }
    return number >= 0 && number < 164 ? u32(cpu, at(kCompositeClasses, number, 4)) : 0u;
}

// FUN_0053c1b0 : l'objet trouvé reçoit 0 en +4.
uint32_t part_class(Cpu& cpu, int32_t number) {
    const uint32_t object = class_object(cpu, number);
    if (object) put32(cpu, object + 4, 0);
    return object;
}

// FUN_0053c2c0, FUN_0053c330, FUN_00540020, FUN_00540090 (this, type) : une table de `this`
// indexée par le type, lue pour un type composé ou de 100 à 114, 0 sinon.
uint32_t composite_table(Cpu& cpu, uint32_t self, uint32_t offset, int32_t type) {
    const bool listed = (type_composite(type) & 0xFF) != 0 || (type >= 100 && type <= 0x72);
    return listed ? u32(cpu, self + offset + static_cast<uint32_t>(type) * 4) : 0u;
}
uint32_t parts_of(Cpu& cpu, uint32_t self, int32_t type) { return composite_table(cpu, self, 0x270, type); }
uint32_t extras_of(Cpu& cpu, uint32_t self, int32_t type) { return composite_table(cpu, self, 0x5E4, type); }
int32_t parts_count(Cpu& cpu, uint32_t self, int32_t type) {
    return static_cast<int32_t>(composite_table(cpu, self, 0x958, type));
}
int32_t extras_count(Cpu& cpu, uint32_t self, int32_t type) {
    return static_cast<int32_t>(composite_table(cpu, self, 0xCCC, type));
}

// Décalage de la pièce tourné selon l'orientation (l'original compare orientation × 2 à 0, 2 et
// 4 ; toute autre valeur est la quatrième orientation).
void rotated(Cpu& cpu, uint32_t part, uint32_t orientation, int32_t& x, int32_t& y) {
    const int32_t dx = i32(cpu, part), dy = i32(cpu, part + 4), size = i32(cpu, part + 0xC);
    const auto minus = [](int32_t a, int32_t b) {
        return static_cast<int32_t>(1u - static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
    };
    switch (orientation * 2) {
        case 0: x = dx, y = dy; break;
        case 2: x = dy, y = minus(dx, size); break;
        case 4: x = minus(dx, size), y = minus(dy, size); break;
        default: x = minus(dy, size), y = dx; break;
    }
}

// FUN_00546620(pièce ; orientation, &x, &y). EAX : y pour les orientations 1 et 2, l'adresse de
// y pour les autres.
uint32_t offset_rotate(Cpu& cpu, uint32_t part, uint32_t orientation, uint32_t out_x, uint32_t out_y) {
    int32_t x, y;
    rotated(cpu, part, orientation, x, y);
    put_i32(cpu, out_x, x);
    put_i32(cpu, out_y, y);
    return orientation * 2 == 2 || orientation * 2 == 4 ? static_cast<uint32_t>(y) : out_y;
}

// Méthodes des classes, lues dans leur code : « MOV EAX,n ; RET » pour la taille (méthode 0),
// « XOR AL,AL ; RET » ou « MOV AL,1 ; RET » pour l'entrée (méthode +0xC0).
std::optional<int32_t> class_size(Cpu& cpu, uint32_t object) {
    const uint32_t method = u32(cpu, u32(cpu, object));
    const uint8_t* code = cpu.host(method);
    if (code[0] != 0xB8 || code[5] != 0xC3) return std::nullopt;
    int32_t value;
    std::memcpy(&value, code + 1, 4);
    return value;
}
std::optional<bool> class_entry(Cpu& cpu, uint32_t object) {
    const uint8_t* code = cpu.host(u32(cpu, u32(cpu, object) + 0xC0));
    if (code[0] == 0x32 && code[1] == 0xC0 && code[2] == 0xC3) return false;
    if (code[0] == 0xB0 && code[1] == 0x01 && code[2] == 0xC3) return true;
    return std::nullopt;
}

// Vrai si toutes les pièces d'une liste ont une classe dont la méthode voulue est connue.
bool parts_known(Cpu& cpu, uint32_t list, int32_t count, bool size, bool entry) {
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t object = class_object(cpu, i32(cpu, at(list, i, kPartSize) + 8));
        if (!object) {
            if (size) return false;  // l'original lirait la table de méthodes à l'adresse 0
            continue;
        }
        if (size && !class_size(cpu, object)) return false;
        if (entry && !class_entry(cpu, object)) return false;
    }
    return true;
}

// FUN_0053d900(pièce) : taille de la pièce, méthode 0 de sa classe.
std::optional<uint32_t> part_size_call(Cpu& cpu) {
    const uint32_t part = cpu.reg(Cpu::Reg::ECX);
    const int32_t number = i32(cpu, part + 8);
    const uint32_t object = class_object(cpu, number);
    if (!object) return std::nullopt;
    const std::optional<int32_t> size = class_size(cpu, object);
    if (!size) return std::nullopt;
    put32(cpu, object + 4, 0);
    return static_cast<uint32_t>(*size);
}

// --- FUN_0053cc30 : case de la première pièce qui peut servir d'entrée ------------------------

struct FindPart {
    uint32_t list = 0;
    int32_t count = 0;
    uint32_t eax = 0;  // EAX de l'original si aucune pièce ne convient
};

// Liste parcourue : la seconde liste (objet 0x00FE5100) si elle existe, sinon la première.
std::optional<FindPart> find_part_list(Cpu& cpu, uint32_t self, int32_t type) {
    FindPart f;
    f.list = extras_of(cpu, kCompositeTables, type);
    if (f.list) {
        f.count = extras_count(cpu, self, type);
    } else {
        f.list = parts_of(cpu, self, type);
        if (!f.list) return f;
        f.count = parts_count(cpu, self, type);
    }
    f.eax = with_al(static_cast<uint32_t>(f.count), false);
    if (!parts_known(cpu, f.list, f.count, false, true)) return std::nullopt;
    return f;
}

// Parcourt la liste ; écrit la case trouvée et renvoie EAX.
uint32_t find_part(Cpu& cpu, const FindPart& f, int32_t x, int32_t y, uint32_t orientation, uint32_t out_x,
                   uint32_t out_y) {
    for (int32_t i = 0; i < f.count; ++i) {
        const uint32_t part = at(f.list, i, kPartSize);
        int32_t px, py;
        rotated(cpu, part, orientation, px, py);
        const uint32_t object = part_class(cpu, i32(cpu, part + 8));
        if (object && *class_entry(cpu, object)) {
            put_i32(cpu, out_x, static_cast<int32_t>(static_cast<uint32_t>(px) + static_cast<uint32_t>(x)));
            put_i32(cpu, out_y, static_cast<int32_t>(static_cast<uint32_t>(py) + static_cast<uint32_t>(y)));
            return with_al(out_x, true);
        }
    }
    return f.eax;
}

// FUN_0053cc30(this ; type, x, y, orientation, &x trouvé, &y trouvé) -> AL.
std::optional<uint32_t> find_part_call(Cpu& cpu) {
    const uint32_t self = cpu.reg(Cpu::Reg::ECX);
    const int32_t type = static_cast<int32_t>(cpu.arg(0));
    const std::optional<FindPart> f = find_part_list(cpu, self, type);
    if (!f) return std::nullopt;
    return find_part(cpu, *f, static_cast<int32_t>(cpu.arg(1)), static_cast<int32_t>(cpu.arg(2)), cpu.arg(3),
                     cpu.arg(4), cpu.arg(5));
}

// --- FUN_0053cd80 : rectangle couvert par les pièces -------------------------------------------

struct Bounds {
    uint32_t parts = 0, extras = 0;
    int32_t parts_count = 0, extras_count = 0;
};

std::optional<Bounds> bounds_lists(Cpu& cpu, uint32_t self, int32_t type) {
    Bounds b;
    b.parts = parts_of(cpu, self, type);
    if (!b.parts) return b;
    b.parts_count = parts_count(cpu, self, type);
    b.extras_count = extras_count(cpu, self, type);
    b.extras = extras_of(cpu, kCompositeTables, type);
    if (b.extras_count > 0 && !b.extras) return std::nullopt;
    if (!parts_known(cpu, b.parts, b.parts_count, true, false) ||
        !parts_known(cpu, b.extras, b.extras_count, true, false))
        return std::nullopt;
    return b;
}

// Écrit le rectangle (bornes incluses) comme l'original, en relisant chaque borne en mémoire ;
// renvoie EAX.
uint32_t bounds(Cpu& cpu, const Bounds& b, int32_t x, int32_t y, uint32_t orientation, uint32_t min_x,
                uint32_t min_y, uint32_t max_x, uint32_t max_y, bool skip_1006) {
    put_i32(cpu, min_x, i32(cpu, kMapBounds) - 1);
    put_i32(cpu, min_y, i32(cpu, kMapBounds + 4) - 1);
    put32(cpu, max_x, 0);
    put32(cpu, max_y, 0);
    if (!b.parts) return 0;
    auto add = [&](uint32_t part) {
        int32_t px, py;
        rotated(cpu, part, orientation, px, py);
        px = static_cast<int32_t>(static_cast<uint32_t>(px) + static_cast<uint32_t>(x));
        py = static_cast<int32_t>(static_cast<uint32_t>(py) + static_cast<uint32_t>(y));
        const int32_t size = *class_size(cpu, part_class(cpu, i32(cpu, part + 8)));
        const int32_t far_x = static_cast<int32_t>(static_cast<uint32_t>(size) + static_cast<uint32_t>(px) - 1);
        const int32_t far_y = static_cast<int32_t>(static_cast<uint32_t>(size) + static_cast<uint32_t>(py) - 1);
        if (i32(cpu, min_x) >= px) put_i32(cpu, min_x, px);
        if (i32(cpu, min_y) >= py) put_i32(cpu, min_y, py);
        if (i32(cpu, max_x) <= far_x) put_i32(cpu, max_x, far_x);
        if (i32(cpu, max_y) <= far_y) put_i32(cpu, max_y, far_y);
    };
    for (int32_t i = 0; i < b.parts_count; ++i) add(at(b.parts, i, kPartSize));
    if (b.extras_count <= 0) return b.extras;
    for (int32_t i = 0; i < b.extras_count; ++i) {
        const uint32_t part = at(b.extras, i, kPartSize);
        if (skip_1006 && i32(cpu, part + 8) == 0x3EE) continue;
        add(part);
    }
    return 0;
}

// FUN_0053cd80(this ; type, x, y, orientation, &x min, &y min, &x max, &y max, sauter 1006).
std::optional<uint32_t> bounds_call(Cpu& cpu) {
    const uint32_t self = cpu.reg(Cpu::Reg::ECX);
    const std::optional<Bounds> b = bounds_lists(cpu, self, static_cast<int32_t>(cpu.arg(0)));
    if (!b) return std::nullopt;
    return bounds(cpu, *b, static_cast<int32_t>(cpu.arg(1)), static_cast<int32_t>(cpu.arg(2)), cpu.arg(3), cpu.arg(4),
                  cpu.arg(5), cpu.arg(6), cpu.arg(7), (cpu.arg(8) & 0xFF) != 0);
}

// --- FUN_0053f810 : case de route la plus proche autour du bâtiment -----------------------------

// FUN_00418b00(case) : hors route, la case voisine vers la case d'ancrage.
int32_t step_to_anchor(Cpu& cpu, int32_t tile) {
    if (u8(cpu, at(kTileFlags, tile, 4)) & 0x40) return tile;
    const uint8_t bits = u8(cpu, at(kTileBits, tile, 1));
    switch (bits & 7) {
        case 0: return (bits & 0x38) == 8 ? tile + kMapColumns : tile - kMapColumns;
        case 1: return tile + 1;
        default: return tile - 1;
    }
}

struct RoadSearch {
    int32_t x, y;               // case du bâtiment
    int32_t rank = 12;          // rang du réseau de la meilleure case (11 : hors des grands)
    int32_t distance = 999999;  // carré de sa distance
    int32_t best_x = 0, best_y = 0;
};

void consider(Cpu& cpu, RoadSearch& s, int32_t tile, int32_t column, int32_t row) {
    const int16_t building = static_cast<int16_t>(s16(cpu, at(kTileBuilding, tile, 2)));
    if (building != 0) {
        const int32_t kind = s16(cpu, at(kBuildings, building, 0x118));
        if (kind == 0x7E || kind == 0x76) {
            tile = step_to_anchor(cpu, tile);
            const int32_t offset = tile - i32(cpu, kMapOrigin);
            column = offset % kMapColumns;
            row = offset / kMapColumns;
        }
    }
    const uint32_t terrain = u32(cpu, at(kTileFlags, tile, 4));
    if (!(terrain & 0x40) || (terrain & 4)) return;
    const uint32_t network = u8(cpu, at(kTileRoadNetwork, tile, 1));
    int32_t rank = 11;
    for (int32_t i = 0; i < 10; ++i)
        if (u32(cpu, at(kRoadNetworks, i, 8)) == network) {
            rank = i;
            break;
        }
    const uint32_t dx = static_cast<uint32_t>(column) - static_cast<uint32_t>(s.x);
    const uint32_t dy = static_cast<uint32_t>(row) - static_cast<uint32_t>(s.y);
    const int32_t distance = static_cast<int32_t>(dy * dy + dx * dx);
    if (rank < s.rank || (rank == s.rank && distance < s.distance)) {
        s.rank = rank;
        s.distance = distance;
        s.best_x = column;
        s.best_y = row;
    }
}

}  // namespace

// FUN_0053f810(this ; type, x, y, orientation, &x, &y) -> AL : une pièce d'entrée, sinon la case
// de route la plus proche sur le pourtour du rectangle des pièces (rangée du dessus, du dessous,
// colonne de gauche, de droite), en préférant les grands réseaux (0x00FEB0FC, dix premiers).
// `entry` : ESP à l'entrée de la fonction, sous lequel elle range son rectangle.
std::optional<uint32_t> composite_road_entry(Cpu& cpu, uint32_t self, int32_t type, int32_t x, int32_t y,
                                             uint32_t orientation, uint32_t out_x, uint32_t out_y, uint32_t entry) {
    const std::optional<FindPart> f = find_part_list(cpu, self, type);
    const std::optional<Bounds> b = bounds_lists(cpu, self, type);
    if (!f || !b) return std::nullopt;
    const uint32_t found = find_part(cpu, *f, x, y, orientation, out_x, out_y);
    if (found & 0xFF) return found;
    // Le rectangle va, comme dans l'original, dans ses variables locales, sous la pile de l'appelant.
    const uint32_t min_x = entry - 0x0C, min_y = entry - 0x1C, max_x = entry - 0x18, max_y = entry - 0x20;
    bounds(cpu, *b, x, y, orientation, min_x, min_y, max_x, max_y, false);
    struct { int32_t min_x, min_y, max_x, max_y; } r{i32(cpu, min_x), i32(cpu, min_y), i32(cpu, max_x), i32(cpu, max_y)};
    RoadSearch s{x, y};
    s.best_x = x, s.best_y = y;
    const int32_t origin = i32(cpu, kMapOrigin);
    for (const int32_t row : {r.min_y - 1, r.max_y + 1})
        for (int32_t column = r.min_x; column <= r.max_x; ++column)
            consider(cpu, s, origin + row * kMapColumns + column, column, row);
    for (const int32_t column : {r.min_x - 1, r.max_x + 1})
        for (int32_t row = r.min_y; row <= r.max_y; ++row)
            consider(cpu, s, origin + row * kMapColumns + column, column, row);
    if (s.distance != 999999) {
        put_i32(cpu, out_x, s.best_x);
        put_i32(cpu, out_y, s.best_y);
        return with_al(static_cast<uint32_t>(s.best_x), true);
    }
    put_i32(cpu, out_x, x);
    put_i32(cpu, out_y, y);
    return with_al(out_y, false);
}

bool composite_type(int32_t type) { return (type_composite(type) & 0xFF) != 0; }
uint32_t composite_parts(Cpu& cpu, uint32_t self, int32_t type) { return parts_of(cpu, self, type); }
void composite_offset(Cpu& cpu, uint32_t part, uint32_t orientation, int32_t& x, int32_t& y) {
    rotated(cpu, part, orientation, x, y);
}
bool composites_known() { return composites_installed; }

namespace {

std::optional<uint32_t> road_entry_call(Cpu& cpu) {
    return composite_road_entry(cpu, cpu.reg(Cpu::Reg::ECX), static_cast<int32_t>(cpu.arg(0)),
                                static_cast<int32_t>(cpu.arg(1)), static_cast<int32_t>(cpu.arg(2)), cpu.arg(3),
                                cpu.arg(4), cpu.arg(5), cpu.reg(Cpu::Reg::ESP));
}

// Code réécrit ici, dans l'ordre des adresses (FUN_00418b00 est reprise dans road_entry).
constexpr uint32_t kRewritten[][2] = {
    {kTileStepToAnchor, kTileStepToAnchorEnd}, {kTypeComposite, kTypeCompositeEnd},
    {kPartClass, kPartClassEnd},               {kCompositeParts, kCompositePartsEnd},
    {kCompositeExtras, kCompositeExtrasEnd},   {kCompositeFindPart, kCompositeFindPartEnd},
    {kCompositeBounds, kCompositeBoundsEnd},   {kCompositePartSize, kCompositePartSizeEnd},
    {kCompositeRoadEntry, kCompositeRoadEntryEnd}, {kCompositePartsCount, kCompositePartsCountEnd},
    {kCompositeExtrasCount, kCompositeExtrasCountEnd}, {kOffsetRotate, kOffsetRotateEnd},
};
constexpr uint64_t kRewrittenHash = 0x11F95702691B2962ull;

// FUN_00534f60 (this = 0x00FE5100), multi_tile_buildings_update, à chaque tour : parcourt les
// bâtiments en service de 1 à [0x0091EBF8], appelle FUN_00534d60 pour un monument (types 100 à
// 114) et saute les fiches des autres pièces d'un bâtiment composé (FUN_00540020). Elle n'écrit
// rien d'autre : sans monument, tout est fait ici. Avec un monument, laissée à l'original.
std::optional<uint32_t> multi_tile_update(Cpu& cpu) {
    const uint32_t self = cpu.reg(Cpu::Reg::ECX);
    for (int32_t i = 1; i <= i32(cpu, kBuildingsLast); ++i) {
        const uint32_t b = at(kBuildings, i, 0x118);
        if (u8(cpu, b - 0x10) != 1) continue;
        const int32_t type = s16(cpu, b);
        if (type >= 100 && type <= 0x72) return std::nullopt;  // FUN_00534d60
        if (!composite_type(type)) continue;
        const int32_t parts = parts_count(cpu, self, type);
        if (parts < 1) return std::nullopt;  // l'original tournerait sur place
        i += parts - 1;
    }
    return u32(cpu, kBuildingsLast);
}

// FUN_0040ead0, appelée par buildings_spawn_walkers : en 0x0040EB09, deux « REP STOSD » effacent
// deux tableaux qui se suivent (2 × 0x1A5E0 entiers de 32 bits, EAX = 0). L'émulateur les fait
// entier par entier : 1 ms par appel dans la grande ville.
uint32_t spawn_clear(Cpu& cpu) {
    if (cpu.reg(Cpu::Reg::EAX) != 0) return 0;
    constexpr uint32_t kBytes = 2 * 0x1A5E0 * 4;
    std::memset(cpu.host(kSpawnGrids), 0, kBytes);
    cpu.set_reg(Cpu::Reg::ECX, 0);
    cpu.set_reg(Cpu::Reg::EDI, kSpawnGrids + kBytes);
    return kSpawnClearEnd;
}

// --- Chantiers des bâtiments composés --------------------------------------------------------
//
// Chaque partie d'un bâtiment composé a sa fiche ; FUN_0053c200(this, bâtiment) rend l'objet de
// la classe de la partie et y écrit le bâtiment en +4, que ses méthodes relisent. Les méthodes
// lues ici ne font que lire la fiche : `building` remplace le champ +4.

// FUN_0053c200 : objet de la classe de la partie, ou 0. `write` : écrit le bâtiment en +4 de
// l'objet trouvé, comme l'original (FUN_0053c1b0 y met d'abord 0).
uint32_t part_object(Cpu& cpu, uint32_t self, int32_t building, bool write) {
    const uint32_t r = at(kBuildings, building, 0x118);
    const int32_t type = s16(cpu, r);
    const uint32_t parts = parts_of(cpu, self, type);
    if (parts == 0) return 0;
    const int32_t index = s16(cpu, r + 2);
    if (index < 0 || index >= parts_count(cpu, self, type)) return 0;
    const uint32_t object = class_object(cpu, i32(cpu, at(parts, index, kPartSize) + 8));
    if (object != 0 && write) put_i32(cpu, object + 4, building);
    return object;
}

uint32_t method(Cpu& cpu, uint32_t object, uint32_t slot) { return u32(cpu, u32(cpu, object) + slot); }

// Méthodes inconnues : le calcul s'arrête et rend la main à l'original.
struct Methods {
    bool known = true;

    // FUN_005358e0 (+0x18) : étapes de la hauteur de la partie, 0 pour un sanctuaire (sauf la
    // partie de classe 0x7A du type 0x62, une étape plus basse).
    int32_t stages(Cpu& cpu, uint32_t object, int32_t building) {
        if (method(cpu, object, 0x18) != kPartStages) return known = false, 0;
        const uint32_t r = at(kBuildings, building, 0x118);
        const int32_t type = s16(cpu, r);
        const uint32_t parts = parts_of(cpu, kCompositeTables, type);
        if (parts == 0) return known = false, 0;
        const uint32_t part = at(parts, s16(cpu, r + 2), kPartSize);
        const bool special = type == 0x62 && i32(cpu, part + 8) == 0x7A;
        if (!special && composite_type(type)) return 0;
        return (i32(cpu, part + 0x10) - (special ? 1 : 0)) / 2 * 4;
    }
    // +0x08 : nombre d'étapes du chantier de la partie.
    int32_t steps(Cpu& cpu, uint32_t object, int32_t building) {
        switch (method(cpu, object, 0x08)) {
            case kPartFramesOne: return 1;
            case kPartFramesPlus1: return stages(cpu, object, building) + 1;
            case kPartFramesPlus2: return stages(cpu, object, building) + 2;
            case kPartFramesPlus3: return stages(cpu, object, building) + 3;
            case kPartFramesPlus4: return stages(cpu, object, building) + 4;
            default: return known = false, 0;
        }
    }
    // FUN_0053e210 (+0x04) : étapes faites, champ +0x48 de la fiche.
    int32_t progress(Cpu& cpu, uint32_t object, int32_t building) {
        if (method(cpu, object, 0x04) != kPartProgress) return known = false, 0;
        return i32(cpu, at(kBuildings, building, 0x118) + 0x48);
    }
    // +0x3C et +0x40 (étape) : poids d'une étape, le même pour toutes.
    int32_t weight(Cpu& cpu, uint32_t object, uint32_t slot) {
        switch (method(cpu, object, slot)) {
            case kPartWeightOne: return 1;
            case kPartWeightTwo: return 2;
            case kPartWeightFour: return 4;
            default: return known = false, 0;
        }
    }
    // FUN_00541670 (+0x60) : la partie est finie.
    bool finished(Cpu& cpu, uint32_t object, int32_t building) {
        if (method(cpu, object, 0x60) != kPartFinished) return known = false, true;
        return progress(cpu, object, building) >= steps(cpu, object, building) - 1;
    }
    // Méthodes « XOR AL,AL ; RET » et « MOV AL,1 ; RET ».
    bool flag(Cpu& cpu, uint32_t object, uint32_t slot) {
        const uint32_t m = method(cpu, object, slot);
        if (m != kReturnFalse && m != kReturnTrue) known = false;
        return m == kReturnTrue;
    }
};

// FUN_0053eca0(this, bâtiment, compte rendu) sans compte rendu (ses sept appelants passent 0,
// sauf 0x00487ECB : EBX) : avancement du chantier en pour cent. Pour chaque partie, la somme des
// poids (+0x40) des étapes 1 à étapes − 1, et celle des étapes déjà faites ; 100 si rien à faire.
// Elle n'écrit que le bâtiment en +4 des objets (FUN_0053c200, partie par partie).
std::optional<uint32_t> site_progress(Cpu& cpu) {
    if (cpu.arg(1) != 0) return std::nullopt;
    const uint32_t self = cpu.reg(Cpu::Reg::ECX);
    const int32_t building = static_cast<int32_t>(cpu.arg(0));
    const int32_t first = building - s16(cpu, at(kBuildings, building, 0x118) + 2);
    const int32_t count = parts_count(cpu, self, s16(cpu, at(kBuildings, first, 0x118)));
    int32_t total = 0, done = 0;
    for (const bool write : {false, true}) {
        Methods m;
        total = done = 0;
        for (int32_t i = first; i < first + count; ++i) {
            const uint32_t object = part_object(cpu, self, i, write);
            if (object == 0) continue;
            const int32_t steps = m.steps(cpu, object, i), progress = m.progress(cpu, object, i);
            for (int32_t step = 1; step < steps; ++step) {
                m.weight(cpu, object, 0x3C);
                const int32_t w = m.weight(cpu, object, 0x40);
                total += w;
                if (progress >= step) done += w;
            }
        }
        if (!m.known) return std::nullopt;
    }
    if (total == 0) return 100u;
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(done) * 100u) / total);
}

// FUN_0053e740(this, bâtiment, x) : cherche, par plusieurs passes sur les parties du chantier
// dans un ordre de priorité, une partie que la méthode +0x50 accepte, et rend son numéro. Seul le
// cas où aucune passe n'appelle +0x50 est fait ici (la réponse est alors 0) : chantier marqué
// (bit 0x100 du champ +0x54 de la fiche), ou toutes les parties finies et aucune à servir. Les
// passes ne lisent que la fiche ; les objets reçoivent le bâtiment en +4 partie par partie, dans
// le même ordre à chaque passe. Vrai quand le cas est le bon, après les écritures si `write`.
bool site_idle(Cpu& cpu, uint32_t self, int32_t first, bool write) {
    const uint32_t r = at(kBuildings, first, 0x118);
    if (u32(cpu, r + 0x54) & 0x100) return true;
    const int32_t type = s16(cpu, r);
    const int32_t end = first + parts_count(cpu, self, type);
    Methods m;
    const auto each = [&](auto served) {
        for (int32_t i = first; i < end; ++i) {
            const uint32_t object = part_object(cpu, self, i, false);
            if (object != 0 && served(object, i)) return false;
        }
        return true;
    };
    bool any = true, upper = true, lower = true;  // 0x0053E792, 0x0053E797, 0x0053E79C
    each([&](uint32_t o, int32_t i) {
        if (m.finished(cpu, o, i)) return false;
        if (m.flag(cpu, o, 0xA0)) any = false;
        if (m.flag(cpu, o, 0xB0) || m.flag(cpu, o, 0xB4)) upper = false;
        if (m.flag(cpu, o, 0xA4) || m.flag(cpu, o, 0xA8)) lower = false;
        return false;
    });
    const bool full = type == 0x62 && any && upper && lower;  // 0x0053E84F
    bool idle = true;
    if (full || (type >= 100 && type <= 0x72))  // 0x0053E872 : passe triée
        idle = each([&](uint32_t o, int32_t i) {
            return !m.flag(cpu, o, 0x0C) || (m.flag(cpu, o, 0xBC) && !m.finished(cpu, o, i));
        });
    idle = idle && each([&](uint32_t o, int32_t i) { return m.flag(cpu, o, 0xA0) && !m.finished(cpu, o, i); }) &&
           each([&](uint32_t o, int32_t i) {
               return (m.flag(cpu, o, 0xB0) || m.flag(cpu, o, 0xB4)) && !m.finished(cpu, o, i);
           }) &&
           each([&](uint32_t o, int32_t i) {
               return (m.flag(cpu, o, 0xA4) || m.flag(cpu, o, 0xA8)) && !m.finished(cpu, o, i);
           }) &&
           (!full || each([&](uint32_t o, int32_t i) { return !m.flag(cpu, o, 0xC8) && !m.finished(cpu, o, i); })) &&
           each([&](uint32_t o, int32_t i) { return !m.finished(cpu, o, i); });
    if (!idle || !m.known) return false;
    if (write)
        for (int32_t i = first; i < end; ++i) part_object(cpu, self, i, true);
    return true;
}

std::optional<uint32_t> site_need(Cpu& cpu) {
    const uint32_t self = cpu.reg(Cpu::Reg::ECX);
    const int32_t first = static_cast<int32_t>(cpu.arg(0));
    if (!site_idle(cpu, self, first, true)) return std::nullopt;
    return 0u;
}

// FUN_0053ec10(this, …, x) : premier chantier de bâtiment composé, parmi les fiches 1 à 3 999, où
// FUN_0053e740 trouve une partie à servir. Faite ici quand aucun n'en a (réponse 0) ; sinon, ou si
// le nombre de parties d'un chantier est nul (l'original repasserait sur la même fiche), laissée
// à l'original avant toute écriture.
std::optional<uint32_t> site_search(Cpu& cpu) {
    const uint32_t self = cpu.reg(Cpu::Reg::ECX);
    for (const bool write : {false, true})
        for (int32_t i = 1; i < 4000; ++i) {
            const uint32_t r = at(kBuildings, i, 0x118);
            if (u8(cpu, r - 0x10) != 1) continue;
            const int32_t type = s16(cpu, r);
            if (!composite_type(type) && (type < 100 || type > 0x72)) continue;
            if (type != 0x75 && !site_idle(cpu, self, i, write)) return std::nullopt;
            const int32_t parts = parts_count(cpu, self, type);
            if (parts < 1) return std::nullopt;
            i += parts - 1;
        }
    return 0u;
}

constexpr uint32_t kSites[][2] = {
    {kSiteProgress, kSiteProgressEnd}, {kSiteNeed, kSiteNeedEnd},   {kSiteSearch, kSiteSearchEnd},
    {kPartObject, kPartObjectEnd},     {0x005358E0, 0x0053598E},   {0x0053E210, 0x0053E22E},
    {0x005504B0, 0x005504C9},          {0x00536E40, 0x00536E47},   {0x0055DB50, 0x0055DB68},
    {0x0053B7B0, 0x0053B7C8},          {0x0053E520, 0x0053E528},   {0x00541670, 0x0054168B},
    {0x00565930, 0x00565933},          {0x00565940, 0x00565943},
};
constexpr uint64_t kSitesHash = 0xA00965C6181D064Dull;

// Code lu par ces deux-là (FUN_005344a0 et FUN_00540020 sont dans kRewritten).
constexpr uint32_t kScanned[][2] = {{kMultiTileUpdate, kMultiTileUpdateEnd}, {kSpawnClear, kSpawnClearEnd}};
constexpr uint64_t kScannedHash = 0x2341B3AF6DBF6C4Dull;

template <size_t N>
uint64_t fingerprint(Cpu& cpu, const uint32_t (&ranges)[N][2]) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& range : ranges)
        for (uint32_t a = range[0]; a < range[1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_buildings(Process& p) {
    Cpu& cpu = p.cpu();
    if (fingerprint(cpu, kRewritten) != kRewrittenHash) return 0;
    const bool scanned = fingerprint(cpu, kScanned) == kScannedHash;
    const bool sites = fingerprint(cpu, kSites) == kSitesHash;
    cpu.replace_function(kTypeComposite, 4, [](Cpu& c) { return type_composite(static_cast<int32_t>(c.arg(0))); });
    cpu.replace_function(kPartClass, 4, [](Cpu& c) { return part_class(c, static_cast<int32_t>(c.arg(0))); });
    cpu.replace_function(kCompositeParts, 4, [](Cpu& c) {
        return parts_of(c, c.reg(Cpu::Reg::ECX), static_cast<int32_t>(c.arg(0)));
    });
    cpu.replace_function(kCompositeExtras, 4, [](Cpu& c) {
        return extras_of(c, c.reg(Cpu::Reg::ECX), static_cast<int32_t>(c.arg(0)));
    });
    cpu.replace_function(kCompositePartsCount, 4, [](Cpu& c) {
        return static_cast<uint32_t>(parts_count(c, c.reg(Cpu::Reg::ECX), static_cast<int32_t>(c.arg(0))));
    });
    cpu.replace_function(kCompositeExtrasCount, 4, [](Cpu& c) {
        return static_cast<uint32_t>(extras_count(c, c.reg(Cpu::Reg::ECX), static_cast<int32_t>(c.arg(0))));
    });
    cpu.replace_function(kOffsetRotate, 0x0C, [](Cpu& c) {
        return offset_rotate(c, c.reg(Cpu::Reg::ECX), c.arg(0), c.arg(1), c.arg(2));
    });
    // Octets déplacés, sans adresse relative : « MOV EAX,[ECX+8] ; MOV ECX,0x00FE5100 »,
    // « SUB ESP,0x14 ; PUSH EBX ; MOV EBX,[ESP+0x1C] », « SUB ESP,0xC ; MOV EAX,[0x00DA2F78] »,
    // « MOV EAX,[ESP+0x18] ; SUB ESP,0x24 ».
    cpu.add_fast_path(kCompositePartSize, 0, 8, part_size_call);
    cpu.add_fast_path(kCompositeFindPart, 0x18, 8, find_part_call);
    cpu.add_fast_path(kCompositeBounds, 0x24, 8, bounds_call);
    cpu.add_fast_path(kCompositeRoadEntry, 0x18, 7, road_entry_call);
    composites_installed = true;
    if (!scanned) return 11;
    // « MOV EAX,[0x0091EBF8] » et « MOV ECX,0x1A5E0 ; MOV EDI,0x00A3FDC4 » déplacés.
    cpu.add_fast_path(kMultiTileUpdate, 0, 5, multi_tile_update);
    cpu.add_code_jump(kSpawnClear, 10, spawn_clear);
    if (!sites) return 13;
    // « PUSH EBP ; MOV EBP,ESP ; AND ESP,-8 », « MOV EAX,0x7D14 »,
    // « PUSH EBX ; PUSH EBP ; PUSH ESI ; PUSH EDI ; MOV EBX,ECX » déplacés.
    cpu.add_fast_path(kSiteProgress, 8, 6, site_progress);
    cpu.add_fast_path(kSiteNeed, 8, 5, site_need);
    cpu.add_fast_path(kSiteSearch, 0x0C, 6, site_search);
    return 16;
}

}  // namespace zeus::win32
