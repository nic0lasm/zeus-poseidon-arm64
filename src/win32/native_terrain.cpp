// Fonctions du terrain réécrites pour l'hôte : étapes 1 à 4 de docs/engine/terrain.md §10.
//
// Ce sont les premières fonctions natives de la simulation : leurs résultats entrent dans l'état
// du jeu (le générateur aléatoire l'écrit directement), si bien que la comparaison de l'état tour
// par tour (docs/equivalence.md) les juge, en plus de la vérification appel par appel.
//
//   1. FUN_004488c0 : le générateur aléatoire (§5). Ses copies en ligne dans d'autres fonctions
//      restent émulées.
//   2. FUN_00496010, FUN_00496080 : dans le losange jouable, près de son bord (§2). Réécrites
//      pour FUN_004a0dc0 natif, mais pas interceptées : l'appel d'une fonction native par
//      l'émulateur coûte environ 140 ns, plus que leur quinzaine d'instructions émulées. Sur
//      poseidon (28 millions d'appels), les intercepter rendait le scénario 12 % plus lent.
//   3. FUN_0049cae0 : nombre des 8 voisines portant un masque (§5).
//   4. FUN_004a0dc0 : rectangle libre pour bâtir (§9), et FUN_00496b50 : rectangle contenant un
//      point réservé de la carte.
//
// Les motifs de voisinage de l'étape 3 (FUN_004a2510, FUN_004a2180, FUN_004a2590, FUN_004a2ac0)
// ne sont appelés par aucun des scénarios : rien ne prouverait leur réécriture, ils restent à
// l'original.
//
// Chaque fonction rend aussi les bits hauts de EAX que l'original y laisse (les tests ne
// posent que AL) : le banc d'équivalence compare EAX entier. Une fonction n'est remplacée que si
// son code a l'empreinte attendue (FNV-1a sur toute la fonction).

#include <cstdint>
#include <cstring>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

// Lectures et écritures dans la mémoire du jeu ; adresses calculées en 32 bits, comme dans le
// jeu, y compris pour un indice négatif.
uint32_t u32(Cpu& cpu, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, cpu.host(address), 4);
    return value;
}

int32_t i32(Cpu& cpu, uint32_t address) { return static_cast<int32_t>(u32(cpu, address)); }

int16_t s16(Cpu& cpu, uint32_t address) {
    int16_t value;
    std::memcpy(&value, cpu.host(address), 2);
    return value;
}

void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }

int32_t arg(Cpu& cpu, int n) { return static_cast<int32_t>(cpu.arg(n)); }

// Résultat d'un test : AL vaut 0 ou 1, les autres octets ceux du dernier calcul de l'original.
uint32_t with_al(uint32_t eax, bool value) { return (eax & 0xFFFFFF00u) | (value ? 1u : 0u); }

uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t a = start; a < end; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

// FUN_004488c0 : range le dernier tirage de 7 bits dans l'anneau de 100, puis fait avancer les
// deux registres à décalage de 31 bits de 31 pas (bit 0 xor bit 4 réinjecté en bit 30) et en
// tire 15, 7 et 3 bits. Rend en EAX les 15 bits du second registre.
uint32_t random_next(Cpu& cpu) {
    int32_t slot = i32(cpu, kRandomRingIndex);
    put32(cpu, kRandomRing + 4 * static_cast<uint32_t>(slot), u32(cpu, kRandom7));
    ++slot;
    put32(cpu, kRandomRingIndex, slot < 100 ? static_cast<uint32_t>(slot) : 0u);
    uint32_t a = u32(cpu, kRandomA), b = u32(cpu, kRandomB);
    for (int step = 0; step < 31; ++step) {
        const uint32_t in_a = ((a >> 4) ^ a) & 1, in_b = ((b >> 4) ^ b) & 1;
        a >>= 1;
        b >>= 1;
        if (in_a) a |= 0x40000000;
        if (in_b) b |= 0x40000000;
    }
    put32(cpu, kRandomA, a);
    put32(cpu, kRandom15, a & 0x7FFF);
    put32(cpu, kRandom7, a & 0x7F);
    put32(cpu, kRandom3, a & 7);
    put32(cpu, kRandomB, b);
    put32(cpu, kRandomB15, b & 0x7FFF);
    put32(cpu, kRandomB7, b & 0x7F);
    put32(cpu, kRandomB3, b & 7);
    return b & 0x7FFF;
}

// Le losange jouable : deux bornes de colonne et de rangée (kDiamondX, kDiamondY) séparent ses
// quatre côtés, chacun un demi-plan sur x + y ou x - y.
struct Diamond {
    int32_t x, y, left, right, top;
};

Diamond diamond(Cpu& cpu) {
    return {i32(cpu, kDiamondX), i32(cpu, kDiamondY), i32(cpu, kDiamondLeft), i32(cpu, kDiamondRight),
            i32(cpu, kDiamondTop)};
}

// FUN_00496010(x, y) : la case est-elle dans le losange ?
uint32_t in_play_area(Cpu& cpu, int32_t x, int32_t y) {
    const Diamond d = diamond(cpu);
    const auto u = [](int32_t v) { return static_cast<uint32_t>(v); };
    if (x <= d.x) {
        if (y <= d.y) return with_al(u(x + y), x + y >= d.left + d.y);
        return with_al(u(x - y), x - y >= d.x - d.top);
    }
    if (y <= d.y) return with_al(u(x - y), x - y <= d.right - d.y);
    return with_al(u(x + y), x + y <= d.right + d.y + 1);
}

// FUN_00496080(x, y, m) : dans le losange, à moins de m cases de son bord.
uint32_t near_play_edge(Cpu& cpu, int32_t x, int32_t y, int32_t m) {
    const uint32_t inside = in_play_area(cpu, x, y);
    if ((inside & 0xFF) == 0) return inside;
    const Diamond d = diamond(cpu);
    const auto u = [](int32_t v) { return static_cast<uint32_t>(v); };
    if (x <= d.x) {
        if (y <= d.y) return with_al(u(d.left + d.y), x - m + y < d.left + d.y);
        return with_al(u(d.x - d.top), x - y - m < d.x - d.top);
    }
    if (y <= d.y) return with_al(u(d.y), x - y + m > d.right - d.y);
    return with_al(u(d.right + d.y + 1), x + y + m > d.right + d.y + 1);
}

// FUN_0049cae0(masque, case) : nombre des 8 voisines dont la nature porte un des bits du masque.
uint32_t count_neighbours(Cpu& cpu) {
    static constexpr int32_t kNeighbours[] = {-228, -227, 1, 229, 228, 227, -1, -229};  // N, NE… NO
    const uint32_t mask = cpu.arg(0);
    const int32_t tile = arg(cpu, 1);
    uint32_t count = 0;
    for (const int32_t offset : kNeighbours)
        if (u32(cpu, kTileFlags + 4 * static_cast<uint32_t>(tile + offset)) & mask) ++count;
    return count;
}

// FUN_00496b50(x1, y1, x2, y2) : un des points réservés de la carte est-il dans le rectangle ?
// Trois listes de 4 points, les x puis les y : 16 bits, 32 bits, 16 bits. L'original parcourt
// les y avec un pointeur et le laisse dans EAX.
uint32_t area_has_marker(Cpu& cpu) {
    const int32_t x1 = arg(cpu, 0), y1 = arg(cpu, 1), x2 = arg(cpu, 2), y2 = arg(cpu, 3);
    const int32_t left = x1 < x2 ? x1 : x2, right = x1 > x2 ? x1 : x2;
    const int32_t top = y1 < y2 ? y1 : y2, bottom = y1 > y2 ? y1 : y2;
    const auto inside = [&](int32_t x, int32_t y) { return x >= left && x <= right && y >= top && y <= bottom; };
    for (uint32_t ys = kMarkersA + 8; ys < kMarkersA + 16; ys += 2)
        if (inside(s16(cpu, ys - 8), s16(cpu, ys))) return with_al(ys, true);
    for (uint32_t ys = kMarkersB + 16; ys < kMarkersB + 32; ys += 4)
        if (inside(i32(cpu, ys - 16), i32(cpu, ys))) return with_al(ys, true);
    for (uint32_t ys = kMarkersC + 8; ys < kMarkersC + 16; ys += 2)
        if (inside(s16(cpu, ys - 8), s16(cpu, ys))) return with_al(ys, true);
    return with_al(kMarkersC + 16, false);
}

// FUN_004a0dc0(x, y, l, h, masque, image, permet_10000, ignore_personnages, strict) : le rectangle
// de l x h cases est-il libre pour bâtir ? Règles et ordre : docs/engine/terrain.md §9.
// L'original garde deux indicateurs dans les octets bas des emplacements de x et de y (« une case
// porte 0x10000 », « une case ne le porte pas ») et la case de départ dans kBuildOrigin : ils sont
// écrits ici de même.
uint32_t area_buildable(Cpu& cpu) {
    const int32_t x = arg(cpu, 0), y = arg(cpu, 1), w = arg(cpu, 2), h = arg(cpu, 3);
    const uint32_t mask = cpu.arg(4) | 0x10010000;
    const uint32_t image = cpu.arg(5);
    const bool allow_10000 = (cpu.arg(6) & 0xFF) != 0, ignore_walkers = (cpu.arg(7) & 0xFF) != 0,
               strict = (cpu.arg(8) & 0xFF) != 0;
    if (x < 3 || y < 3 || x + w - 1 >= i32(cpu, kMapWidth) - 3 || y + h - 1 >= i32(cpu, kMapHeight) - 3) return 0;
    const uint32_t args = cpu.reg(Cpu::Reg::ESP) + 4;
    uint8_t* with_10000 = cpu.host(args);       // octet bas de x
    uint8_t* without_10000 = cpu.host(args + 4);  // octet bas de y
    *with_10000 = 0;
    *without_10000 = 0;
    const int32_t origin = i32(cpu, kMapOffset) + 228 * y + x;
    put32(cpu, kBuildOrigin, static_cast<uint32_t>(origin));
    const uint32_t allowed = strict ? 0x3DFFDEEF : allow_10000 ? 0x3DFED66F : 0x3DFFD66F;
    for (int32_t row = 0; row < h; ++row) {
        const uint32_t offsets = kBuildOffsets + 0x30 * static_cast<uint32_t>(row);
        for (int32_t column = 0; column < w; ++column) {
            const int32_t tile = i32(cpu, offsets + 8 * static_cast<uint32_t>(column)) + origin;
            if (near_play_edge(cpu, tile % 228, tile / 228, 3) & 0xFF) return 0;
            const uint32_t t = static_cast<uint32_t>(tile);
            const uint32_t flags = u32(cpu, kTileFlags + 4 * t) & mask;
            const uint32_t object = u32(cpu, kTileObject + 4 * t);
            if (object != 0 && *cpu.host(object + 0x18) == 0) return 0;
            if (flags & 0x10000) {
                if (*without_10000) return 0;
                *with_10000 = 1;
            } else {
                if (*with_10000) return 0;
                *without_10000 = 1;
            }
            if (flags & allowed) return 0;
            // Personnages de la case : seuls les types 24 et 6 sont permis, sauf ignore_personnages.
            for (int32_t figure = s16(cpu, kTileFigure + 2 * t); figure != 0;) {
                const uint32_t record = kFigures + 0x184 * static_cast<uint32_t>(figure);
                const uint8_t type = *cpu.host(record + 2);
                if (type != 0x18 && type != 6) {
                    if (!ignore_walkers) return 0;
                    break;
                }
                figure = s16(cpu, record);
            }
            if (image != 0 && u32(cpu, kTileImage + 4 * t) != image - 1) return 0;
        }
    }
    return 1;
}

struct Replacement {
    uint32_t address, end;
    uint64_t hash;
    uint32_t (*fn)(Cpu&);  // nul : code vérifié, dont dépend une autre réécriture, laissé à l'original
};

constexpr Replacement kReplacements[] = {
    {kRandomNext, kRandomNextEnd, 0xC9FF4B6F41258CC9ull, random_next},
    {kInPlayArea, kInPlayAreaEnd, 0x5CB83714115FC874ull, nullptr},
    {kNearPlayEdge, kNearPlayEdgeEnd, 0xEA0DD650ED256EF8ull, nullptr},
    {kCountNeighbours, kCountNeighboursEnd, 0xC60CABF5237BECCFull, count_neighbours},
    {kAreaHasMarker, kAreaHasMarkerEnd, 0x4913E2834F326D44ull, area_has_marker},
    {kAreaBuildable, kAreaBuildableEnd, 0xE0F3CA6B096803E2ull, area_buildable},
};

}  // namespace

int install_native_terrain(Process& p) {
    Cpu& cpu = p.cpu();
    // FUN_004a0dc0 appelle FUN_00496080, qui appelle FUN_00496010 : sa version native suppose les
    // deux autres conformes, d'où une vérification de toutes avant d'en remplacer une.
    for (const Replacement& r : kReplacements)
        if (fingerprint(cpu, r.address, r.end) != r.hash) return 0;
    int installed = 0;
    for (const Replacement& r : kReplacements)
        if (r.fn) {
            cpu.replace_function(r.address, 0, r.fn);
            ++installed;
        }
    return installed;
}

}  // namespace zeus::win32
