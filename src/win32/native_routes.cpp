// Recherche de routes de la simulation réécrite pour l'hôte (docs/engine/simulation.md, § 5 et 8).
//
// Toutes ces fonctions sont des parcours en largeur sur la carte de 228 × 228 cases, avec les
// mêmes variables : la grille des distances 0x01079460 (distance depuis le départ + 1, 0 pour une
// case non atteinte), effacée en entier (0xCB10 entrées) au début de chaque parcours, et la file
// circulaire 0x010AC0A0, dont la tête et la queue restent en mémoire. Les voisines d'une case sont
// prises dans l'ordre haut, droite, bas, gauche (−228, +1, +228, −1). Elles ne diffèrent que par
// la case qui est praticable, par ce qui arrête le parcours et par ce qu'elles renvoient :
//
//   - les parcours « à masque » (FUN_00588920 et sept copies, FUN_0058a4d0, FUN_0058a8c0) : une
//     case est praticable si son mot de la grille 0x0105FE40 porte un bit du masque ; ils vont
//     jusqu'à une case, ou jusqu'à une case d'une liste ;
//   - FUN_00587fb0 : terre nue (terrain & 0x1D7FDC0F = 0x800) ;
//   - FUN_00588b20 : jusqu'à une case d'une liste, marquée d'abord dans la grille 0x00FFA580 ;
//   - FUN_00588d30 : masque 0x13D, et une case de bâtiment seulement pour trois types ;
//   - FUN_00503a60 : nature 0x01046820 positive (l'eau : hypothèse) ;
//   - FUN_00502ee0 : un cache du dernier trajet, puis soit un parcours pondéré par la nature des
//     cases (la case de plus petite distance de la file est cherchée à chaque pas), soit un
//     parcours simple où un bâtiment n'est traversable que par son propriétaire.
//
// Chacune passe par un chemin rapide (Cpu::add_fast_path) : un appel coûte ici des centaines de
// milliers d'instructions émulées (l'effacement de la grille en est 52 000), si bien que le coût
// du crochet ne compte pas. Les rares entrées que l'original ne termine pas en temps raisonnable
// (liste de longueur négative) lui sont rendues.
//
// Les indices de case sont calculés comme dans l'original, en 32 bits : une voisine hors de la
// carte lit les mêmes octets que lui. Adresses : win32/game_gog.h. Le code n'est modifié que si
// tous les octets réécrits ici sont ceux attendus (empreinte FNV-1a, comparée avant toute
// modification).

#include <cstdint>
#include <cstring>
#include <optional>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

constexpr int32_t kMapColumns = 228;   // cases par rangée des grilles de la carte (0xE4)
constexpr int32_t kCells = 0xCB10;     // entrées effacées de la grille des distances, places de la file
constexpr int32_t kQueueLast = kCells - 1;

// Accès à la mémoire émulée par indice de case, l'adresse calculée en 32 bits comme l'original.
struct Memory {
    uint8_t* base;
    uint8_t* at(uint32_t address) const { return base + address; }
    int32_t i32(uint32_t address) const {
        int32_t v;
        std::memcpy(&v, at(address), 4);
        return v;
    }
    void put(uint32_t address, int32_t v) const { std::memcpy(at(address), &v, 4); }
    uint32_t cell(uint32_t grid, int32_t tile, uint32_t size) const {
        return grid + static_cast<uint32_t>(tile) * size;
    }
    int32_t distance(int32_t tile) const { return i32(cell(kRouteDistances, tile, 4)); }
    void set_distance(int32_t tile, int32_t d) const { put(cell(kRouteDistances, tile, 4), d); }
    uint16_t mask(int32_t tile) const {
        uint16_t v;
        std::memcpy(&v, at(cell(kRouteMask, tile, 2)), 2);
        return v;
    }
    int8_t kind(int32_t tile) const { return static_cast<int8_t>(*at(cell(kRouteKind, tile, 1))); }
    uint32_t terrain(int32_t tile) const { return static_cast<uint32_t>(i32(cell(kTileFlags, tile, 4))); }
    int32_t building(int32_t tile) const {
        int16_t v;
        std::memcpy(&v, at(cell(kTileBuilding, tile, 2)), 2);
        return v;
    }
};

// Case (x, y) : [0x00DA2F80] + 228 y + x.
int32_t tile_of(const Memory& m, uint32_t x, uint32_t y) {
    return static_cast<int32_t>(static_cast<uint32_t>(m.i32(kMapOrigin)) + y * kMapColumns + x);
}

// La file : tête et queue gardées ici, rangées en mémoire à la fin du parcours (rien d'autre ne
// les lit pendant).
class Queue {
public:
    explicit Queue(const Memory& m) : m_(m) {}
    int32_t head = 0, tail = 0;

    int32_t at(int32_t index) const { return m_.i32(m_.cell(kRouteQueue, index, 4)); }
    void set(int32_t index, int32_t tile) const { m_.put(m_.cell(kRouteQueue, index, 4), tile); }
    int32_t front() const { return at(head); }
    void push(int32_t tile, int32_t distance) {
        m_.set_distance(tile, distance);
        set(tail, tile);
        if (++tail > kQueueLast) tail = 0;
    }
    // Passe à la case suivante ; faux quand la file est vide.
    bool pop() {
        if (++head > kQueueLast) head = 0;
        return head != tail;
    }
    void store() const {
        m_.put(kRouteQueueHead, head);
        m_.put(kRouteQueueTail, tail);
    }

private:
    const Memory& m_;
};

// Début commun : distances effacées, compteur, départ à la distance 1, seul dans la file.
// `stale` : valeur rangée en 0x010DF2C0 (1 sauf pour FUN_00502ee0).
void begin(const Memory& m, Queue& q, int32_t start, int32_t stale) {
    std::memset(m.at(kRouteDistances), 0, kCells * 4);
    m.put(kRouteStale, stale);
    m.put(kRouteSearches, m.i32(kRouteSearches) + 1);
    m.put(kRouteStart, start);
    m.set_distance(start, 1);
    q.set(0, start);
    q.head = 0;
    q.tail = 1;
}

constexpr int32_t kNeighbours[4] = {-kMapColumns, 1, kMapColumns, -1};

// Parcours simple : `stop(case)` dit si le parcours s'arrête sur la case sortie de la file,
// `open(voisine)` si une voisine non atteinte est praticable. Renvoie vrai si arrêté par `stop`.
template <class Stop, class Open>
bool breadth_first(const Memory& m, Queue& q, Stop stop, Open open) {
    do {
        const int32_t tile = q.front();
        if (stop(tile)) return true;
        const int32_t d = m.distance(tile) + 1;
        for (const int32_t step : kNeighbours) {
            const int32_t next = tile + step;
            if (m.distance(next) == 0 && open(next)) q.push(next, d);
        }
    } while (q.pop());
    return false;
}

// Fin des parcours vers une case : atteinte (et d'indice positif quand `signed_check`).
uint32_t reached(const Memory& m, int32_t target, bool signed_check) {
    if (signed_check && target < 0) return 0;
    return m.distance(target) != 0 ? 1u : 0u;
}

// FUN_00588920 et ses copies (x0, y0, x1, y1) : vers une case par les cases du masque.
uint32_t mask_to_tile(Cpu& cpu, uint16_t mask) {
    const Memory m{cpu.host(0)};
    Queue q(m);
    begin(m, q, tile_of(m, cpu.arg(0), cpu.arg(1)), 1);
    const int32_t target = tile_of(m, cpu.arg(2), cpu.arg(3));
    breadth_first(m, q, [&](int32_t t) { return t == target; }, [&](int32_t n) { return (m.mask(n) & mask) != 0; });
    q.store();
    return reached(m, target, true);
}

// FUN_0058a4d0 (x, y, liste, n), FUN_0058a8c0 (`forward`) : jusqu'à une case de la
// liste ; renvoie son rang + 1 (le dernier de la liste qui convient, ou le premier), 0 sinon.
std::optional<uint32_t> mask_to_list(Cpu& cpu, uint16_t mask, bool forward) {
    const int32_t count = static_cast<int32_t>(cpu.arg(3));
    if (count < 0 && !forward) return std::nullopt;  // l'original parcourt alors 4 milliards de rangs
    const Memory m{cpu.host(0)};
    const uint32_t list = cpu.arg(2);
    Queue q(m);
    begin(m, q, tile_of(m, cpu.arg(0), cpu.arg(1)), 1);
    int32_t found = -1;
    const auto listed_at = [&](int32_t i, int32_t t) { return m.i32(list + static_cast<uint32_t>(i) * 4) == t; };
    const auto stop = [&](int32_t t) {
        if (forward) {
            for (int32_t i = 0; i < count && found < 0; ++i)
                if (listed_at(i, t)) found = i;
        } else {
            for (int32_t i = count - 1; i >= 0 && found < 0; --i)
                if (listed_at(i, t)) found = i;
        }
        return found >= 0;
    };
    breadth_first(m, q, stop, [&](int32_t n) { return (m.mask(n) & mask) != 0; });
    q.store();
    return static_cast<uint32_t>(found + 1);
}

// FUN_00587fb0(x0, y0, x1, y1) : vers une case par la terre nue.
uint32_t on_land(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    Queue q(m);
    begin(m, q, tile_of(m, cpu.arg(0), cpu.arg(1)), 1);
    const int32_t target = tile_of(m, cpu.arg(2), cpu.arg(3));
    breadth_first(m, q, [&](int32_t t) { return t == target; },
                  [&](int32_t n) { return (m.terrain(n) & 0x1D7FDC0Fu) == 0x800; });
    q.store();
    return reached(m, target, true);
}

// FUN_00588b20(x, y, liste, n) : les cases de la liste reçoivent leur rang + 1 dans la grille
// 0x00FFA580 (le premier rang l'emporte), puis parcours par le masque 0x11D jusqu'à une case
// marquée ; renvoie sa marque, 0 sinon.
std::optional<uint32_t> listed(Cpu& cpu) {
    const int32_t count = static_cast<int32_t>(cpu.arg(3));
    if (count < 0) return std::nullopt;
    const Memory m{cpu.host(0)};
    std::memset(m.at(kRouteListMarks), 0, kCells * 4);
    const uint32_t list = cpu.arg(2);
    for (int32_t i = count - 1; i >= 0; --i) {
        const int32_t t = m.i32(list + static_cast<uint32_t>(i) * 4);
        if (t >= 0 && t < kCells) m.put(m.cell(kRouteListMarks, t, 4), i + 1);
    }
    Queue q(m);
    begin(m, q, tile_of(m, cpu.arg(0), cpu.arg(1)), 1);
    int32_t mark = 0;
    breadth_first(m, q, [&](int32_t t) { return (mark = m.i32(m.cell(kRouteListMarks, t, 4))) != 0; },
                  [&](int32_t n) { return (m.mask(n) & 0x11D) != 0; });
    q.store();
    return mark - 1 < 0 ? 0u : static_cast<uint32_t>(mark);
}

// FUN_004a96e0(bâtiment) : type 0x23, 0x24 ou 0xD9.
bool building_passable(const Memory& m, int32_t building) {
    int16_t type;
    std::memcpy(&type, m.at(m.cell(kBuildings, building, 0x118)), 2);
    return (type >= 0x23 && type <= 0x24) || type == 0xD9;
}

// FUN_00588d30(x0, y0, x1, y1) : comme FUN_00588920 avec le masque 0x13D, une case de bâtiment
// (terrain & 0x10000000) ne passant que si FUN_004a96e0 l'accepte.
uint32_t through_buildings(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    Queue q(m);
    begin(m, q, tile_of(m, cpu.arg(0), cpu.arg(1)), 1);
    const int32_t target = tile_of(m, cpu.arg(2), cpu.arg(3));
    breadth_first(m, q, [&](int32_t t) { return t == target; }, [&](int32_t n) {
        if (!(m.mask(n) & 0x13D)) return false;
        return !(m.terrain(n) & 0x10000000u) || building_passable(m, m.building(n));
    });
    q.store();
    return reached(m, target, true);
}

// FUN_00503a60(x0, y0, x1, y1) : vers une case par les cases de nature positive.
uint32_t ship(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    Queue q(m);
    begin(m, q, tile_of(m, cpu.arg(0), cpu.arg(1)), 1);
    const int32_t target = tile_of(m, cpu.arg(2), cpu.arg(3));
    breadth_first(m, q, [&](int32_t t) { return t == target; }, [&](int32_t n) { return m.kind(n) >= 0; });
    q.store();
    return reached(m, target, false);
}

// Coût d'entrée dans une case selon sa nature (FUN_00502ee0, parcours pondéré), -1 interdite.
int32_t kind_cost(int8_t kind) {
    static constexpr int32_t kCost[6] = {0, 10, 0, 40, 60, 2};
    return kind >= 0 && kind < 6 ? kCost[kind] : -1;
}

// FUN_00502ee0(x0, y0, x1, y1, propriétaire, pas) : trajet de (x0, y0) à (x1, y1), vrai s'il existe.
//
// Le dernier trajet reste valable tant que 0x010DF2C0 est nul : avec le mode 1 en 0x010DF2C4, dès
// que le départ a une distance ; sinon pour les mêmes quatre coordonnées, l'octet 0x010DF2D8 nul
// et le départ et l'arrivée atteints. Sinon les distances sont recalculées :
//   - propriétaire nul : à chaque pas, la case de la file de plus petite distance (recherche
//     linéaire, arrêtée dès une distance au plus égale à la plus petite déjà prise) en sort, et
//     la case de tête prend sa place ; entrer dans une voisine coûte 1 plus le coût de sa nature ;
//     au plus `pas` cases sorties, et la case du dessous est à [0x005D9414] ;
//   - sinon, parcours simple par les natures 0 et 2, et 1 si le bâtiment de la case est le
//     propriétaire.
// L'original range la case d'arrivée dans l'emplacement de son deuxième argument.
uint32_t route_search(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    const uint32_t x0 = cpu.arg(0), y0 = cpu.arg(1), x1 = cpu.arg(2), y1 = cpu.arg(3);
    const int32_t owner = static_cast<int32_t>(cpu.arg(4)), limit = static_cast<int32_t>(cpu.arg(5));
    const int32_t start = tile_of(m, x0, y0), target = tile_of(m, x1, y1);
    m.put(kRouteStart, start);
    m.put(cpu.reg(Cpu::Reg::ESP) + 8, target);
    const auto cached = [&](uint32_t offset) { return static_cast<uint32_t>(m.i32(kRouteCache + offset)); };
    if (m.i32(kRouteStale) == 0) {
        if (cached(0) == 1) {
            if (m.distance(start) != 0) return 1;
        } else if (cached(4) == x0 && cached(8) == y0 && cached(0xC) == x1 && cached(0x10) == y1 &&
                   *m.at(kRouteCache + 0x14) == 0 && m.distance(start) != 0 && m.distance(target) != 0) {
            return 1;
        }
    }
    m.put(kRouteCache + 4, static_cast<int32_t>(x0));
    m.put(kRouteCache + 8, static_cast<int32_t>(y0));
    m.put(kRouteCache + 0xC, static_cast<int32_t>(x1));
    m.put(kRouteCache + 0x10, static_cast<int32_t>(y1));
    m.put(kRouteSearchesWeighted, m.i32(kRouteSearchesWeighted) + 1);
    Queue q(m);
    begin(m, q, start, 0);

    if (owner != 0) {
        breadth_first(m, q, [&](int32_t t) { return t == target; }, [&](int32_t n) {
            const int8_t kind = m.kind(n);
            return kind == 0 || kind == 2 || (kind == 1 && m.building(n) == owner);
        });
        q.store();
        return m.distance(target) != 0 ? 1u : 0u;
    }

    const int32_t below = m.i32(kRouteRowStep);
    const int32_t steps_to[4] = {-kMapColumns, 1, below, -1};
    int32_t threshold = 0, taken = 0;
    for (;;) {
        int32_t best = q.head;
        int32_t smallest = m.distance(q.at(q.head));
        if (smallest > threshold) {
            const auto consider = [&](int32_t i) {
                const int32_t d = m.distance(q.at(i));
                if (d >= smallest) return false;
                best = i;
                smallest = d;
                return d <= threshold;
            };
            if (q.tail > q.head) {
                for (int32_t i = q.head + 1; i < q.tail; ++i)
                    if (consider(i)) break;
            } else {
                bool stopped = false;
                for (int32_t i = q.head + 1; i <= kQueueLast && !stopped; ++i) stopped = consider(i);
                if (!stopped && smallest > threshold)
                    for (int32_t i = 0; i < q.tail; ++i) {
                        const int32_t d = m.distance(q.at(i));
                        if (d < smallest) best = i, smallest = d;
                    }
            }
            threshold = smallest;
        }
        const int32_t tile = q.at(best);
        if (best != q.head) q.set(best, q.at(q.head));
        if (tile == target || ++taken > limit) break;
        const int32_t d = m.distance(tile) + 1;
        for (const int32_t step : steps_to) {
            const int32_t next = tile + step;
            if (m.distance(next) != 0) continue;
            const int32_t cost = kind_cost(m.kind(next));
            if (cost >= 0) q.push(next, d + cost);
        }
        if (!q.pop()) break;
    }
    q.store();
    return m.distance(target) != 0 ? 1u : 0u;
}

// Parcours à masque : début de chaque copie et son masque (« MOV EBX,masque » ou « MOV EBP,masque »).
// Seules les copies qu'appelle un scénario du banc sont remplacées : FUN_00589ae0 (0x200),
// FUN_005898f0 (0x15C) et FUN_00589ce0 (0x200) restent à l'original.
struct MaskCopy {
    uint32_t address;
    uint16_t mask;
};
// FUN_00588920 et ses copies, 503 octets chacune.
constexpr MaskCopy kToTile[] = {
    {0x005883E0, 0x17D}, {0x00588920, 0x11D}, {0x005894F0, 0x11C}, {0x005896F0, 0x15C},
    {0x00589ED0, 0x13C}, {0x0058A0D0, 0x12C}, {0x0058A2D0, 0x19C}, {0x0058A6C0, 0x10C},
};
constexpr uint32_t kToTileSize = 0x1F7;
// FUN_0058a4d0, 493 octets.
constexpr MaskCopy kToListBackward[] = {{0x0058A4D0, 0x19C}};
constexpr uint32_t kToListBackwardSize = 0x1ED;
constexpr uint16_t kListForwardMask = 0x10C;  // FUN_0058a8c0

// Code réécrit ici, dans l'ordre des adresses.
constexpr uint32_t kRewritten[][2] = {
    {kRouteSearch, kRouteSearchEnd},         {kRouteSearchShip, kRouteSearchShipEnd},
    {kRouteOnLand, kRouteOnLandEnd},         {0x005883E0, 0x005883E0 + kToTileSize},
    {0x00588920, 0x00588920 + kToTileSize},  {kRouteListed, kRouteListedEnd},
    {kRouteBuildings, kRouteBuildingsEnd},   {0x005894F0, 0x005894F0 + kToTileSize},
    {0x005896F0, 0x005896F0 + kToTileSize},  {0x00589ED0, 0x00589ED0 + kToTileSize},
    {0x0058A0D0, 0x0058A0D0 + kToTileSize},  {0x0058A2D0, 0x0058A2D0 + kToTileSize},
    {0x0058A4D0, 0x0058A4D0 + kToListBackwardSize}, {0x0058A6C0, 0x0058A6C0 + kToTileSize},
    {kRouteListForward, kRouteListForwardEnd},
};
constexpr uint64_t kRewrittenHash = 0x01612A2348062208ull;

uint64_t fingerprint(Cpu& cpu) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& range : kRewritten)
        for (uint32_t a = range[0]; a < range[1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_routes(Process& p) {
    Cpu& cpu = p.cpu();
    if (fingerprint(cpu) != kRewrittenHash) return 0;
    int installed = 0;
    // Octets déplacés, sans adresse relative : « PUSH EBX ; PUSH ESI ; PUSH EDI ; MOV ECX,0xCB10 »
    // (8), la même chose avec « PUSH EBP » (9), « PUSH EBP ; PUSH ESI ; PUSH EDI ; MOV ECX,0xCB10 »
    // (8), « PUSH ECX ; MOV EAX,[ESP+0xC] » (5).
    for (const MaskCopy& c : kToTile) {
        cpu.add_fast_path(c.address, 0, 8, [mask = c.mask](Cpu& c2) -> std::optional<uint32_t> {
            return mask_to_tile(c2, mask);
        });
        ++installed;
    }
    for (const MaskCopy& c : kToListBackward) {
        cpu.add_fast_path(c.address, 0, 9, [mask = c.mask](Cpu& c2) { return mask_to_list(c2, mask, false); });
        ++installed;
    }
    cpu.add_fast_path(kRouteListForward, 0, 9, [](Cpu& c) { return mask_to_list(c, kListForwardMask, true); });
    cpu.add_fast_path(kRouteOnLand, 0, 8, [](Cpu& c) -> std::optional<uint32_t> { return on_land(c); });
    cpu.add_fast_path(kRouteListed, 0, 8, listed);
    cpu.add_fast_path(kRouteBuildings, 0, 9, [](Cpu& c) -> std::optional<uint32_t> { return through_buildings(c); });
    cpu.add_fast_path(kRouteSearchShip, 0, 8, [](Cpu& c) -> std::optional<uint32_t> { return ship(c); });
    cpu.add_fast_path(kRouteSearch, 0, 5, [](Cpu& c) -> std::optional<uint32_t> { return route_search(c); });
    return installed + 6;
}

}  // namespace zeus::win32
