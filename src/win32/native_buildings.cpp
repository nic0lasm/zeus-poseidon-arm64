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
// Les valeurs de retour reproduisent tout EAX, octets hauts compris : plusieurs fonctions
// renvoient un booléen dans AL en laissant dans le reste de EAX ce qui s'y trouvait.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si tous les octets réécrits ici sont
// ceux attendus (empreinte FNV-1a, comparée avant toute modification).

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

uint64_t fingerprint(Cpu& cpu) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& range : kRewritten)
        for (uint32_t a = range[0]; a < range[1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_buildings(Process& p) {
    Cpu& cpu = p.cpu();
    if (fingerprint(cpu) != kRewrittenHash) return 0;
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
    return 11;
}

}  // namespace zeus::win32
