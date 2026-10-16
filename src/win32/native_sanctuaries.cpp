// Extras de l'objet d'une case d'ancrage dans le second parcours de FUN_00458170 (étape 3,
// docs/engine/rendering.md §11.2), réécrits pour l'hôte :
//
//   - FUN_0053e5f0(this = 0x00FE5100, bâtiment, x, y), appelée pour les sanctuaires et les
//     monuments (types 0x54 à 0x62, 0x75, 0x64 à 0x72), une fois par case d'ancrage de chaque
//     partie. Elle enchaîne quatre appels, avec les mêmes arguments :
//       FUN_0055ee60 (ne fait rien : « RET 0xC ») ;
//       FUN_00540770 : objet de la classe de la partie (FUN_0053c200), méthode +0x48 (x, y, 0) ;
//       FUN_005406b0 : même objet, méthode +0xF8 (x, y) ;
//       FUN_005406e0 : selon le type, méthode +0xFC (x, y) du même objet.
//     L'objet d'une classe de partie est un objet C++ statique (171 classes, tables 0x00640E08 et
//     0x00641098, 16 octets : table de méthodes, puis le bâtiment courant en +4) ; FUN_0053c200 y
//     écrit le bâtiment à chaque appel. Les méthodes +0x48 (FUN_005407a0, 3 348 octets),
//     +0xF8 (FUN_0044ee80 ou FUN_005367b0) et +0xFC sont celles des tables de méthodes, lues à
//     chaque appel. Seul le cas courant est réécrit, celui où rien n'est dessiné :
//       +0x48 = FUN_005407a0 sans partie différée (0x00FE6148 ≠ 1), sans icônes (0x00FE615C nul),
//         et qui s'arrête soit au test de la méthode +0x0C (FUN_00535600, faux), soit après
//         FUN_00534500 nul et la méthode +0xC4 « faux » (FUN_00565930) ;
//       +0xF8 = FUN_0044ee80 dont les méthodes +0xB0, +0xB4 et +0xB8 sont « faux », ou
//         FUN_005367b0 avec l'octet +0x8F de la fiche nul ;
//       +0xFC, quand elle est appelée, = FUN_0055e510 (« RET 8 »).
//     Il ne reste alors que l'écriture du bâtiment dans l'objet. Tout autre cas (images de la
//     partie, icônes, partie différée, image animée de FUN_0044ee80 par FUN_005af8c0…) revient
//     à l'original, décidé avant toute écriture. Dans working-city, 52 319 des 53 150 appels sont
//     dans le cas réécrit ; les 831 autres sont ceux de FUN_0044ee80 dont la méthode +0xB0 est
//     « vrai » (trois classes), qui dessinent.
//
//   - Les deux tours des bâtiments de types 0x81, 0x91 et 0x92 (0x004593B1 à 0x004594F1) : pour
//     chacune des deux cases voisines de la rangée précédente qui porte aussi un de ces types
//     (FUN_0045a640), FUN_0041fd40 passe au répartiteur de sprites l'image 0x809 (à gauche, en
//     x - 2, y - 43) ou 0x80F (à droite, en x + 29, y - 43), numéros de groupe convertis par
//     FUN_0040fd10. Vue tournée : FUN_005ae9f0, laissé à l'original. Les tours sont traitées
//     quand le répartiteur natif sait dessiner les deux images (sprite_handled), vérifié avant la
//     partie haute du bâtiment.
//
// Seule FUN_0053e5f0 reçoit un chemin rapide : elle n'a qu'un appelant (0x00459375, le dessin),
// et ne sert pas à la simulation. Les autres fonctions des parties composées (0x0053C1B0 et
// suivantes), qui servent aussi à la simulation, ne sont pas interceptées : leur logique est relue
// ici, et leurs octets sont couverts par l'empreinte kSanctuaryCode (game_gog.h). FUN_0041fd40
// (41 appelants) n'est pas interceptée non plus : seuls ses deux appels par les tours sont refaits.

#include "win32/native_sanctuaries.h"

#include <cstring>
#include <iterator>
#include <optional>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

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

// Fiche d'un bâtiment (0x118 octets, kBuildings désigne son champ type).
uint32_t record(int32_t building) { return at(kBuildings, building, 0x118); }
int32_t building_type(Cpu& cpu, int32_t building) { return s16(cpu, record(building)); }

bool sanctuary_code = false;  // kSanctuaryCode vérifiée
bool tower_code = false;      // kTowerCode vérifiée

// Empreinte FNV-1a 64 bits de plusieurs zones de la mémoire du jeu mises bout à bout.
uint64_t fingerprint(Cpu& cpu, const uint32_t (*ranges)[2], size_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < count; ++i)
        for (uint32_t a = ranges[i][0]; a < ranges[i][1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

// --- FUN_0053e5f0 ------------------------------------------------------------------------------

// FUN_005344a0(type) : sanctuaire (0x54 à 0x62, 0x75).
bool sanctuary_type(int32_t type) { return (type >= 0x54 && type <= 0x62) || type == 0x75; }
// FUN_0045dbb0(type) : monument (0x64 à 0x72).
bool monument_type(int32_t type) { return type >= 0x64 && type <= 0x72; }

// FUN_0053c2c0(type) : liste des parties d'un type (entrées de 0x48 octets : +8 classe, +0x10
// hauteur), ou 0. FUN_00540020(type) : leur nombre.
uint32_t parts_of(Cpu& cpu, uint32_t self, int32_t type) {
    return sanctuary_type(type) || monument_type(type) ? u32(cpu, at(self + 0x270, type, 4)) : 0;
}
int32_t part_count(Cpu& cpu, uint32_t self, int32_t type) {
    return sanctuary_type(type) || monument_type(type) ? i32(cpu, at(self + 0x958, type, 4)) : 0;
}

// FUN_0053c200(bâtiment), sans ses écritures : objet de la classe de la partie, ou 0. L'original
// met à 0 puis au bâtiment le champ +4 de l'objet trouvé (FUN_0053c1b0, puis 0x0053C266).
uint32_t part_object(Cpu& cpu, uint32_t self, int32_t building) {
    const int32_t type = building_type(cpu, building);
    const uint32_t parts = parts_of(cpu, self, type);
    if (parts == 0) return 0;
    const int32_t index = s16(cpu, record(building) + 2);
    if (index < 0 || index >= part_count(cpu, self, type)) return 0;
    const int32_t part_class = i32(cpu, at(parts, index, 0x48) + 8);
    if (part_class >= 1000) {
        const int32_t k = part_class - 1000;
        return k >= 0 && k < 7 ? u32(cpu, at(kPartClassesExtra, k, 4)) : 0;
    }
    return part_class >= 0 && part_class < 0xA4 ? u32(cpu, at(kPartClasses, part_class, 4)) : 0;
}

// Méthode `slot` de l'objet.
uint32_t method(Cpu& cpu, uint32_t object, uint32_t slot) { return u32(cpu, u32(cpu, object) + slot); }

// Les méthodes ci-dessous lisent le bâtiment dans le champ +4 de l'objet, que FUN_0053c200 vient
// d'y écrire : `building` le remplace.

// FUN_00535600 (méthode +0x0C) : la partie est-elle à dessiner ? Vrai pour un sanctuaire, sauf la
// partie de classe 0x7A du type 0x62 ; sinon, comparaison de la hauteur du sol de la case du
// bâtiment avec la hauteur de la partie (+0x10) augmentée de l'octet +0x5C de la fiche.
bool part_drawn(Cpu& cpu, int32_t building) {
    const int32_t type = building_type(cpu, building);
    const uint32_t parts = parts_of(cpu, kComposite, type);
    const int32_t index = s16(cpu, record(building) + 2);
    const bool special = type == 0x62 && i32(cpu, at(parts, index, 0x48) + 8) == 0x7A;
    if (!special && sanctuary_type(type)) return true;
    const int32_t limit = i32(cpu, at(parts, index, 0x48) + 0x10) + u8(cpu, record(building) + 0x5C);
    return s8(cpu, kTileHeight + u32(cpu, record(building) - 4)) >= limit;
}

// FUN_005358e0 (méthode +0x18).
int32_t part_stages(Cpu& cpu, int32_t building) {
    if (building == 0) return 0;
    const int32_t type = building_type(cpu, building);
    const uint32_t parts = parts_of(cpu, kComposite, type);
    const int32_t index = s16(cpu, record(building) + 2);
    const bool special = type == 0x62 && i32(cpu, at(parts, index, 0x48) + 8) == 0x7A;
    if (!special && sanctuary_type(type)) return 0;
    int32_t value = i32(cpu, at(parts, index, 0x48) + 0x10);
    if (special) --value;
    return value / 2 * 4;
}

// FUN_0053e210 (méthode +0x04) : champ +0x48 de la fiche.
int32_t part_progress(Cpu& cpu, int32_t building) {
    return building == 0 ? 0 : i32(cpu, record(building) + 0x48);
}

// Méthode +0x08 : FUN_005504b0 (1) ou FUN_005358e0 + 1, 2, 3, 4 ; faux si inconnue.
bool part_frames(Cpu& cpu, uint32_t object, int32_t building, int32_t& frames) {
    switch (method(cpu, object, 0x08)) {
        case kPartFramesOne: frames = 1; return true;
        case kPartFramesPlus1: frames = part_stages(cpu, building) + 1; return true;
        case kPartFramesPlus2: frames = part_stages(cpu, building) + 2; return true;
        case kPartFramesPlus3: frames = part_stages(cpu, building) + 3; return true;
        case kPartFramesPlus4: frames = part_stages(cpu, building) + 4; return true;
        default: return false;
    }
}

// FUN_005407a0(x, y, 0) (méthode +0x48) dans le cas où elle ne dessine rien.
bool part_draw_empty(Cpu& cpu, uint32_t object, int32_t building) {
    if (method(cpu, object, 0x48) != kPartDraw) return false;
    if (i32(cpu, kDeferredPart) == 1 || u8(cpu, kPartIcons) != 0) return false;  // 0x005407B1, 0x00540817
    if (method(cpu, object, 0x0C) != kPartDrawn) return false;
    if (!part_drawn(cpu, building)) return true;  // 0x00540BAA
    // FUN_0053e250 (méthode +0x1C) : rang de l'image, puis FUN_00534500(rang, 0).
    if (method(cpu, object, 0x1C) != kPartFrame || method(cpu, object, 0x18) != kPartStages ||
        method(cpu, object, 0x04) != kPartProgress)
        return false;
    const int32_t stages = part_stages(cpu, building), progress = part_progress(cpu, building);
    const int32_t frame = stages == 0 ? progress : progress < stages ? 0 : progress - stages;
    int32_t frames = 0;
    if (!part_frames(cpu, object, building, frames)) return false;
    if (frame >= 0 && frame < frames) {
        const uint32_t list = u32(cpu, object + 8);
        if (list != 0 && i32(cpu, at(list, frame, 8) + 4) >= 1) return false;  // 0x00541356
    }
    return method(cpu, object, 0xC4) == kReturnFalse;  // 0x00540BD6
}

// Méthode +0xF8 dans le cas où elle ne dessine rien.
bool part_extra_empty(Cpu& cpu, uint32_t object, int32_t building) {
    const uint32_t extra = method(cpu, object, 0xF8);
    if (extra == kPartExtraTwo) return u8(cpu, record(building) + 0x8F) == 0;  // 0x005367D4
    if (extra != kPartExtra) return false;
    // FUN_0044ee80 : FUN_0049b920 (lecture seule) et la méthode +0xCC, puis +0xB0, +0xB4, +0xB8
    // « faux » : retour en 0x0044F18A.
    const uint32_t turned = method(cpu, object, 0xCC);
    return (turned == kReturnFalse || turned == kReturnTrue) && method(cpu, object, 0xB0) == kReturnFalse &&
           method(cpu, object, 0xB4) == kReturnFalse && method(cpu, object, 0xB8) == kReturnFalse;
}

// FUN_00546db0(type) : rang du sanctuaire dans la table de 0x00EF5DF0 (0x54 à 0x62), ou -1.
int32_t sanctuary_rank(int32_t type) {
    if (type < 0x54 || type > 0x62) return -1;
    if (type == 0x55 || type == 0x62) return 1;
    return type == 0x54 ? 0 : type - 0x54;
}

// FUN_005406e0 : la méthode +0xFC est-elle appelée ? `eax` reçoit sinon la valeur que la fonction
// laisse dans EAX : le type (0x00540721), ou le numéro de la première partie x 7 dont l'octet bas
// est remplacé par l'octet +0xCA de sa fiche (0x0054073E).
bool part_last_called(Cpu& cpu, int32_t building, uint32_t& eax) {
    const int32_t type = building_type(cpu, building);
    if (i32(cpu, at(kSanctuaryTable, sanctuary_rank(type), 0x34)) != 0) return true;
    eax = static_cast<uint32_t>(type);
    if (!monument_type(type)) return false;
    const int32_t first = building - s16(cpu, record(building) + 2);
    const uint8_t built = u8(cpu, record(first) + 0xCA);
    eax = ((static_cast<uint32_t>(first) * 7) & 0xFFFFFF00u) | built;
    return built == 0;
}

// FUN_0053e5f0 traitée en natif ? `eax` reçoit alors la valeur que l'original laisse dans EAX,
// celle de FUN_005406e0 : l'objet (ou 0) quand elle cherche l'objet de la partie, sinon celle que
// donne part_last_called. Son seul appelant ne la relit pas, mais la vérification compare EAX.
bool handled(Cpu& cpu, uint32_t self, int32_t building, uint32_t& eax) {
    if (self != kComposite) return false;
    const uint32_t object = part_object(cpu, self, building);
    const bool last = part_last_called(cpu, building, eax);
    if (last) eax = object;
    if (object == 0) return true;  // aucun des appels ne fait rien
    return part_draw_empty(cpu, object, building) && part_extra_empty(cpu, object, building) &&
           (!last || method(cpu, object, 0xFC) == kPartLast);
}

// --- Les deux tours --------------------------------------------------------------------------

// FUN_0040fd10(groupe) avec this = kSpriteBanks : identifiant de la première image du groupe.
int32_t group_image(Cpu& cpu, int32_t group) {
    const int32_t bank = group / 512, index = group % 512;
    if (index == 0) return 0;
    const uint32_t base = kSpriteBanks + static_cast<uint32_t>(bank) * 0xCC44 + 0x1000;
    const uint32_t table = u32(cpu, base + 4);
    const uint32_t entry = index - 1 <= i32(cpu, base + 0xCC38) ? at(table, index - 1, 8) : table;
    return bank * 16384 + i32(cpu, entry + 4);
}

// FUN_0045a640(case) : la case porte un bâtiment de type 0x81, 0x91 ou 0x92.
bool tower_tile(Cpu& cpu, int32_t tile) {
    if (tile >= 0x0FFF0000) return false;
    const int32_t building = s16(cpu, at(kTileBuilding, tile, 2));
    if (building == 0) return false;
    const int32_t type = building_type(cpu, building);
    return type == 0x81 || type == 0x91 || type == 0x92;
}

}  // namespace

bool sanctuary_handled(Cpu& cpu, int32_t building) {
    uint32_t eax = 0;
    return sanctuary_code && handled(cpu, kComposite, building, eax);
}

void draw_sanctuary(Cpu& cpu, int32_t building) {
    if (const uint32_t object = part_object(cpu, kComposite, building))
        put32(cpu, object + 4, static_cast<uint32_t>(building));
}

// Les tours à dessiner : image et position de chacune (FUN_0041fd40, puis le répartiteur).
struct Tower {
    int32_t id, x, y;
};
int towers_of(Cpu& cpu, int32_t first, int32_t second, int32_t x, int32_t y, Tower (&towers)[2]) {
    int count = 0;
    if (tower_tile(cpu, first)) towers[count++] = {group_image(cpu, 0x809), x - 2, y - 0x2B};
    if (tower_tile(cpu, second)) towers[count++] = {group_image(cpu, 0x80F), x + 0x1D, y - 0x2B};
    return count;
}

bool towers_handled(Cpu& cpu, int32_t first, int32_t second, int32_t x, int32_t y) {
    if (!tower_code) return false;
    Tower towers[2];
    const int count = towers_of(cpu, first, second, x, y, towers);
    for (int i = 0; i < count; ++i)
        if (!sprite_handled(cpu, kSpriteBanks, towers[i].id, towers[i].x, towers[i].y, 0, 0)) return false;
    return true;
}

void draw_towers(Cpu& cpu, int32_t first, int32_t second, int32_t x, int32_t y) {
    Tower towers[2];
    const int count = towers_of(cpu, first, second, x, y, towers);
    for (int i = 0; i < count; ++i) draw_sprite(cpu, kSpriteBanks, towers[i].id, towers[i].x, towers[i].y, 0, 0);
}

int install_native_sanctuaries(Process& p) {
    Cpu& cpu = p.cpu();
    tower_code = fingerprint(cpu, kTowerCode, std::size(kTowerCode)) == kTowerCodeHash;
    sanctuary_code = fingerprint(cpu, kSanctuaryCode, std::size(kSanctuaryCode)) == kSanctuaryCodeHash;
    if (!sanctuary_code) return tower_code ? 1 : 0;
    // « PUSH EBX ; MOV EBX,[ESP+0xC] ; PUSH EBP » déplacés.
    cpu.add_fast_path(kSanctuary, 0x0C, 6, [](Cpu& c) -> std::optional<uint32_t> {
        const int32_t building = static_cast<int32_t>(c.arg(0));
        uint32_t eax = 0;
        if (!handled(c, c.reg(Cpu::Reg::ECX), building, eax)) return std::nullopt;
        draw_sanctuary(c, building);
        return eax;
    });
    return tower_code ? 2 : 1;
}

}  // namespace zeus::win32
