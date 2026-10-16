// Animations et compléments des bâtiments réécrits pour l'hôte : FUN_00413380, qui avance
// l'animation de l'image d'une case, FUN_0045d140, qui dessine l'image ainsi choisie, et
// FUN_0044f320 quand elle n'a rien à dessiner.
//
// Au second parcours de FUN_00458170 (native_draw.cpp, étape 5), une case d'ancrage dont l'image
// du sol est animée (+0x1E de son entrée non nul, 0x00459840) passe par 0x0045984B : FUN_00413380
// (seul appel, 0x00459C00) donne l'image courante de l'animation et la range dans l'octet de la
// case en 0x00CB4B08, puis FUN_0045d140 (seul appel, 0x00459C77) la dessine. Ensuite, pour toute
// case d'ancrage, FUN_0044f320 dessine des compléments pour 21 types de bâtiment.
//
// Ce qui est traité, d'après le listage, et seulement les chemins que les neuf scénarios
// parcourent (comptages --count de docs/engine/rendering.md, 11.2) :
//   - FUN_00413380 : bâtiment présent, types sans compteur +0xDC ni règle propre (FUN_0040ab10,
//     types 0x3D, 0x50 à 0x52) ; image sans avance (vitesse nulle dans 0x006668EC) ou qui avance
//     d'un pas et reboucle à 1 (+0x30 de l'entrée nul ; l'aller-retour reste à l'original) ;
//   - FUN_0045d140 : cas 0 du switch de 0x0045D339, et cas 1 quand FUN_0044eab0 ne peut que
//     renvoyer faux (elle n'écrit rien alors) ; ni sanctuaire (FUN_005344a0, FUN_0045dbb0), ni
//     image retournée (FUN_0045dc10), ni bâtiment en rouge ; le sprite, un cas du répartiteur
//     natif ;
//   - FUN_0044f320 : types hors de sa table (0x0044FDC4) ; ses 21 types dessinés restent à
//     l'original (jamais dessinés dans les scénarios).
// Chaque fonction a un chemin rapide (Cpu::add_fast_path) qui décide sans rien écrire, ce qui
// permet de la comparer appel par appel (--verify) ; animated_anchor fait les deux premières pour
// la boucle native.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si ses octets sont ceux attendus
// (empreinte FNV-1a, comparée avant toute modification).

#include "win32/native_animations.h"

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
uint8_t u8(Cpu& cpu, uint32_t address) { return *cpu.host(address); }
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }
void put16(Cpu& cpu, uint32_t address, uint16_t value) { std::memcpy(cpu.host(address), &value, 2); }
void put8(Cpu& cpu, uint32_t address, uint8_t value) { *cpu.host(address) = value; }

uint32_t at(uint32_t base, int32_t index, uint32_t size) { return base + static_cast<uint32_t>(index) * size; }

bool code_known = false;  // tout le code réécrit ici est celui attendu

int32_t building_type(Cpu& cpu, int32_t building) { return s16(cpu, at(kBuildings, building, 0x118)); }
bool red(Cpu& cpu, int32_t building) { return u8(cpu, at(kBuildings + 0xBF, building, 0x118)) != 0; }

// Entrée d'une image dans sa banque (FUN_00423180) : banque = image / 16384, numéro = image %
// 16384, signés ; au-delà du nombre d'entrées, la première.
uint32_t image_entry(Cpu& cpu, int32_t image) {
    const int32_t bank = image / 16384, number = image % 16384;
    const uint32_t banks = at(kSpriteBanks, bank, 0xCC44);
    const uint32_t table = u32(cpu, banks + 0x1000);
    return number > i32(cpu, banks + 0xDC34) ? table : at(table, number, 0x48);
}

// FUN_004200f0(0) et FUN_0040aa20(type), comme native_draw.cpp.
bool poseidon(Cpu& cpu) { return u32(cpu, kGameMode) == 1 && (u32(cpu, kGameSubmode) & 0xFFFF) == 1; }
bool listed_building(Cpu& cpu, int32_t type) {
    for (int32_t i = 5; i >= 0; --i) {
        const uint32_t entry = i == 5 && poseidon(cpu) ? kPoseidonBuildings : at(kBuildingTable, i, 0x34);
        if (i32(cpu, entry + 4) == type) return true;
    }
    return false;
}

// --- FUN_00413380(image, case) : image courante de l'animation ---------------------------------

struct Step {
    int32_t frame = 0;      // valeur renvoyée
    bool write = false;     // l'octet de la case (kTileLife) reçoit `frame`
};

// Décide sans rien écrire ; rien quand le chemin n'est pas traité.
std::optional<Step> animation_step(Cpu& cpu, int32_t image, int32_t tile) {
    const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
    const uint8_t current = u8(cpu, at(kTileLife, tile, 1));
    if (building == 0) return Step{};
    const int32_t type = building_type(cpu, building);
    if (type == 0) return Step{};
    const int32_t occupants = s16(cpu, at(kBuildings + 0x30, building, 0x118));
    // Jusqu'à 0x004135CF : soit un test des occupants (+0x30 positif, sinon 0), soit rien.
    if (type == 0x4A || type == 0x4B || type == 0x3A || type == 0x3E || type == 0x39 ||
        (static_cast<uint32_t>(type - 0x35) <= 0x9F && u8(cpu, kStepKinds + static_cast<uint32_t>(type - 0x35)) == 0)) {
        if (occupants <= 0) return Step{};
    } else {
        if (type == 0x3D || type == 0x50 || type == 0x51 || type == 0x52) return std::nullopt;  // 0x00413426
        if (listed_building(cpu, type)) return std::nullopt;                                   // FUN_0040ab10
        if (static_cast<uint32_t>(type - 0x0F) <= 0x89 && u8(cpu, kStepCounters + static_cast<uint32_t>(type - 0x0F)) == 0)
            return std::nullopt;  // compteur +0xDC de la fiche (0x004134DB)
    }
    // 0x004135CF : FUN_00433d50(type, 5), sauf si l'octet +0xCA est non nul.
    const uint32_t figures = u8(cpu, at(kBuildings + 0xCA, building, 0x118)) != 0 ? 0 : u32(cpu, at(kBuildingFigures, type * 10 + 5, 4));
    if (occupants <= 0 && figures != 0) return Step{};
    const uint32_t entry = image_entry(cpu, image);
    if (u8(cpu, kAnimationTicks + u8(cpu, entry + 0x3A)) == 0) return Step{current & 0x7F, false};
    if (u8(cpu, entry + 0x30) != 0) return std::nullopt;  // aller-retour (0x00413699)
    // 0x00413717 : l'octet entier plus un, ramené à 1 au-delà du nombre d'images (+0x1E, signé).
    int32_t next = current + 1;
    if (next > s16(cpu, entry + 0x1E)) next = 1;
    return Step{next, true};
}

void commit(Cpu& cpu, int32_t tile, const Step& step) {
    if (step.write) put8(cpu, at(kTileLife, tile, 1), static_cast<uint8_t>(step.frame));
}

std::optional<uint32_t> animation_step_call(Cpu& cpu) {
    const int32_t tile = static_cast<int32_t>(cpu.arg(1));
    const std::optional<Step> step = animation_step(cpu, static_cast<int32_t>(cpu.arg(0)), tile);
    if (!step) return std::nullopt;
    commit(cpu, tile, *step);
    return static_cast<uint32_t>(step->frame);
}

// --- FUN_0045d140(bâtiment, image, n, x, y) : dessin de l'image n de l'animation ------------------

// FUN_0044eab0(bâtiment, …) appelée par le cas 1 : vrai quand elle renvoie faux quel que soit le
// stock de la fiche (elle n'écrit alors rien). Table 0x0044ED44 (cas 0 : faux ; cas 5 : 0x0044EB78,
// faux si le stock est positif, sinon table 0x0044EE20, dont le cas 10 est faux) ; hors des
// tables, toujours faux.
bool sign_always_false(Cpu& cpu, int32_t type) {
    const uint32_t t = static_cast<uint32_t>(type - 0x29);
    if (t > 0xAF) return true;
    const uint8_t kind = u8(cpu, kWorkingKinds + t);
    if (kind == 0) return true;
    if (kind != 5) return false;
    return t > 0x5E || u8(cpu, kWorkingStock + t) == 10;
}

// Décide, puis dessine et écrit ce qu'écrit l'original ; rien (et rien d'écrit) quand le cas
// n'est pas traité. `args` : ESP à l'entrée de la fonction, dont les emplacements d'arguments
// image et x sont réécrits comme par l'original (0x0045D1E4, 0x0045D84B) ; 0 depuis la boucle.
std::optional<uint32_t> animation_draw(Cpu& cpu, int32_t building, int32_t image, int32_t frame, int32_t x, int32_t y,
                                       uint32_t args) {
    const uint32_t entry = image_entry(cpu, image);
    const int32_t count = s16(cpu, entry + 0x1E);
    const int32_t shown = frame > count ? count : frame;
    const int32_t half = (s16(cpu, entry + 0x14) + 1) / 2;
    const int32_t id = static_cast<int32_t>(static_cast<uint32_t>(image) + static_cast<uint32_t>(shown));
    const uint32_t slope = u8(cpu, at(kTileSlope, i32(cpu, kViewTile), 1)) & 0x0F;
    const int32_t orientation = i32(cpu, kViewOrientation);
    if ((orientation == 2 || orientation == 6) && building != 0) return std::nullopt;  // FUN_005ae9f0, retournée
    if (red(cpu, building)) return std::nullopt;                                        // surimpression
    const int32_t type = building_type(cpu, building);
    if ((type >= 0x54 && type <= 0x62) || type == 0x75 || (type >= 100 && type <= 0x72))
        return std::nullopt;  // FUN_005344a0, FUN_0045dbb0 : type donné par FUN_00547980
    const uint32_t k = static_cast<uint32_t>(type - 0x16);
    const uint8_t kind = k > 0xC2 ? 5 : u8(cpu, kAnimationKinds + k);
    if (kind == 1 ? !sign_always_false(cpu, type) : kind != 0) return std::nullopt;
    // 0x0045D83D : x + décalage +0x22 de l'entrée ; y selon la pente de la case (0x0045D851 à
    // 0x0045D9DD), 30 pixels par case moins la hauteur de la pente ; au-delà de 5, rien.
    const int32_t xx = static_cast<int32_t>(static_cast<uint32_t>(x) + static_cast<uint32_t>(s16(cpu, entry + 0x22)));
    std::optional<uint32_t> result;
    if (slope <= 5) {
        int32_t lift = 0;
        switch (slope) {
            case 1: lift = i32(cpu, kHalfTileHeight); break;
            case 2: lift = i32(cpu, kTileHeightStep); break;
            case 3: lift = i32(cpu, kTileHeight15); break;
            case 4: lift = i32(cpu, kTileHeight2); break;
            case 5: lift = i32(cpu, kTileHeight2) + i32(cpu, kHalfTileHeight); break;
            default: break;
        }
        const uint32_t yy = static_cast<uint32_t>(s16(cpu, entry + 0x24) - s16(cpu, entry + 0x16)) -
                            static_cast<uint32_t>(lift) + static_cast<uint32_t>(y) + 30 * (slope + 1);
        result = draw_sprite(cpu, kSpriteBanks, id, xx, static_cast<int32_t>(yy), 0, 0);
        if (!result) return std::nullopt;
    }
    if (args) {
        put32(cpu, args + 8, static_cast<uint32_t>(id));
        put32(cpu, args + 0x10, static_cast<uint32_t>(xx));
    }
    put32(cpu, kAnimationHalfWidth, static_cast<uint32_t>(half));
    put8(cpu, kAnimationMirror, 0);
    put16(cpu, kOverlay, 0);
    return result ? *result : slope;  // sans dessin, EAX garde la pente (0x0045D843)
}

std::optional<uint32_t> animation_draw_call(Cpu& cpu) {
    return animation_draw(cpu, static_cast<int32_t>(cpu.arg(0)), static_cast<int32_t>(cpu.arg(1)),
                          static_cast<int32_t>(cpu.arg(2)), static_cast<int32_t>(cpu.arg(3)),
                          static_cast<int32_t>(cpu.arg(4)), cpu.reg(Cpu::Reg::ESP));
}

// --- FUN_0044f320(bâtiment, x, y) : compléments du bâtiment ----------------------------------

// Types hors de la table 0x0044FDC4 (type - 0x25 au-delà de 0xB3, ou cas 21) : la fonction lit la
// fiche et rend type - 0x25 dans EAX (0x0044F3B6), sans rien écrire.
std::optional<uint32_t> extras_call(Cpu& cpu) {
    const int32_t type = building_type(cpu, static_cast<int32_t>(cpu.arg(0)));
    const uint32_t k = static_cast<uint32_t>(type - 0x25);
    if (k <= 0xB3 && u8(cpu, kExtraKinds + k) != 21) return std::nullopt;
    return k;
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

bool animated_anchor(Cpu& cpu, uint32_t esp, int32_t tile, int32_t building, int32_t x, int32_t y) {
    if (!code_known) return false;
    if (building != 0 && red(cpu, building)) return false;  // 0x00459873 : surimpression
    const int32_t type = building_type(cpu, building);
    // FUN_004574c0 pour 0x39 ; sprite de 0x00459BF0 pour 0x3A, 0x3D et 0x3E.
    if (type == 0x39 || type == 0x3A || type == 0x3D || type == 0x3E) return false;
    const int32_t image = i32(cpu, at(kTileImage, tile, 4));  // [ESP+0x2C] (0x004595E5 ; 0x00459B9D pour 0x3C)
    const std::optional<Step> step = animation_step(cpu, image, tile);
    if (!step) return false;
    if (step->frame > 0) {
        // Le dessin ne lit ni l'octet de la case ni les variables écrites ensuite.
        if (!animation_draw(cpu, building, image, step->frame, x, y, 0)) return false;
    } else if (type == 0x75 || type == 0x37) {
        return false;  // 0x00459C1D, 0x00459C5E : FUN_0045d140 ou FUN_00485310 selon la fiche
    }
    commit(cpu, tile, *step);
    put32(cpu, esp + 0x2C, static_cast<uint32_t>(image));
    put32(cpu, esp + 0x30, static_cast<uint32_t>(step->frame));  // 0x00459C08
    put16(cpu, kOverlay, 0);                                     // 0x00459C7F
    return true;
}

int install_native_animations(Process& p) {
    Cpu& cpu = p.cpu();
    int installed = 0;
    // Code réécrit : FUN_00413380, FUN_0045d140, FUN_0044eab0 avec leurs tables, kAnimationHelpers.
    code_known = fingerprint(cpu, kAnimationStep, kAnimationStepEnd) == kAnimationStepHash &&
                 fingerprint(cpu, kAnimationDraw, kAnimationDrawEnd) == kAnimationDrawHash &&
                 fingerprint(cpu, kWorkingSign, kWorkingSignEnd) == kWorkingSignHash &&
                 fingerprint(cpu, kAnimationHelpers, std::size(kAnimationHelpers)) == kAnimationHelpersHash;
    if (code_known) {
        cpu.add_fast_path(kAnimationStep, 0, 7, animation_step_call);  // « SUB ESP,0xC ; MOV ECX,[ESP+0x14] »
        cpu.add_fast_path(kAnimationDraw, 0, 8, animation_draw_call);  // « SUB ESP,0x1C ; PUSH EBX ; MOV EBX,[ESP+0x28] »
        installed += 2;
    }
    if (fingerprint(cpu, kBuildingExtras, kBuildingExtrasEnd) == kBuildingExtrasHash) {
        cpu.add_fast_path(kBuildingExtras, 0, 7, extras_call);  // « MOV ECX,[ESP+4] ; SUB ESP,0x4C »
        ++installed;
    }
    return installed;
}

}  // namespace zeus::win32
