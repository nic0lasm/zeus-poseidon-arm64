// Personnages de la simulation réécrits pour l'hôte (docs/engine/simulation.md, § 2 et 8). Le
// dessin des personnages est ailleurs (native_walkers.cpp).
//
// Premier lot : la boucle de walkers_update (0x004CC8C0), dernière partie du tour, où l'on entre
// par un saut. Elle parcourt les 2 000 fiches de personnages (de la fiche 1 à la fiche 1 999) ;
// pour chaque fiche en service (octet +6 non nul), elle :
//   1. vérifie le lien +0x7C : s'il désigne une fiche qui n'est pas en vie (+6 ≠ 1) ou dont le
//      lien +0x7A ne revient pas à celle-ci, il est effacé ;
//   2. compte le personnage (0x00EF57E8) et ouvre sa minuterie (FUN_004cc730) : type dans
//      0x00DB78A0, GetTickCount dans 0x00DB78A4 ;
//   3. appelle le gestionnaire de son type (table 0x0063969C, 40 octets par type), ou
//      FUN_004cc160 si le drapeau 0x80000 de +0x104 est mis et que le type n'est pas 0x2B ;
//   4. ferme la minuterie (FUN_004cc760) : par type, 32 octets à 0x00DB6EE0, nombre de passages,
//      somme des durées et plus longue durée ;
//   5. si la fiche est à retirer (+6 = 2), le type 0x44 libère sa place et la fiche est effacée.
// Le numéro de la fiche en cours est 0x00EF57EC.
//
// Sur les 2 000 fiches, quelques centaines sont en service : dans la grande ville de
// `working-city`, 308 par tour. La boucle émulée passait 11 % des octets exécutés du tour à sauter
// les fiches libres et les minuteries faisaient deux appels de l'hôte (GetTickCount) par
// personnage. Ici, deux aiguillages (Cpu::add_code_jump) font tout ce qui sépare deux
// gestionnaires : un seul passage par l'hôte par personnage.
//   - En haut de la boucle (0x004CC8D5, EDX = numéro de fiche) : cherche la prochaine fiche en
//     service, fait les étapes 1 et 2 et poursuit à l'appel du gestionnaire (0x004CC973, ou
//     0x004CC96C pour FUN_004cc160), ou à la fin de la fonction (0x004CCA24).
//   - À l'entrée de FUN_004cc760, appelée depuis la boucle (retour en 0x004CC98C) : ferme la
//     minuterie, dépile l'adresse de retour et, sauf fiche à retirer (étape 5 laissée à
//     l'original, en 0x004CC98C), enchaîne sur la fiche suivante comme en haut de la boucle.
// Les registres que le code d'origine relit à ces adresses sont mis aux valeurs qu'il y aurait.
// GetTickCount est l'horloge du processus (Process::ticks), appelée autant de fois et dans le même
// ordre que par l'original : sous horloge déterministe chaque appel avance l'horloge. Quand
// QueryPerformanceCounter est en service (0x00EF57FC non nul), tout est laissé à l'original.
//
// Lots suivants : les gestionnaires des hoplites (FUN_004b31f0), des cavaliers (FUN_004b26b0) et
// des porte-étendards (FUN_004b1840), par des chemins rapides, quand le soldat tient sa place dans
// son groupe (voir hoplite() plus bas).
//
// Les indices et adresses sont calculés en 32 bits comme dans l'original. Adresses :
// win32/game_gog.h. Le code n'est modifié que si ses octets sont ceux attendus (empreinte FNV-1a,
// comparée avant toute modification).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

constexpr uint32_t kRecord = 0x184;  // octets d'une fiche de personnage
constexpr int32_t kFigureCount = 2000;

struct Memory {
    uint8_t* base;
    uint8_t* at(uint32_t address) const { return base + address; }
    uint32_t u32(uint32_t address) const {
        uint32_t v;
        std::memcpy(&v, at(address), 4);
        return v;
    }
    void put32(uint32_t address, uint32_t v) const { std::memcpy(at(address), &v, 4); }
    int32_t s16(uint32_t address) const {
        int16_t v;
        std::memcpy(&v, at(address), 2);
        return v;
    }
    void put16(uint32_t address, int16_t v) const { std::memcpy(at(address), &v, 2); }
    uint8_t u8(uint32_t address) const { return *at(address); }
    int32_t s8(uint32_t address) const { return static_cast<int8_t>(*at(address)); }
    void put8(uint32_t address, uint8_t v) const { *at(address) = v; }
    int32_t i32(uint32_t address) const { return static_cast<int32_t>(u32(address)); }
};

uint32_t record(int32_t figure) { return static_cast<uint32_t>(figure) * kRecord; }

// Ce que walkers_update fait d'une fiche à l'autre, depuis le haut de sa boucle sur la fiche
// `figure` jusqu'à l'appel du gestionnaire suivant ; renvoie l'adresse où poursuivre.
uint32_t next_figure(Cpu& cpu, const Process& process, int32_t figure) {
    const Memory m{cpu.host(0)};
    while (figure < kFigureCount && m.u8(kFigurePlaced + record(figure)) == 0) ++figure;
    m.put32(kFigureCurrent, static_cast<uint32_t>(figure));
    if (figure >= kFigureCount) {
        cpu.set_reg(Cpu::Reg::EDX, static_cast<uint32_t>(figure));
        return kWalkersUpdateEnd;
    }
    const uint32_t r = record(figure);
    // Étape 1 : le lien +0x7C (0x004CC8EC).
    const int32_t linked = m.s16(kFigureLinkedTo + r);
    if (linked != 0) {
        const uint32_t l = record(linked);
        if (m.u8(kFigurePlaced + l) != 1) m.put16(kFigureLinkedTo + r, 0);
        if (m.s16(kFigureLinkedBack + l) != figure) m.put16(kFigureLinkedTo + r, 0);
        cpu.set_reg(Cpu::Reg::ESI, static_cast<uint32_t>(linked));
    }
    // Étape 2 : le compte, puis la minuterie (FUN_004cc730).
    m.put32(kFiguresUpdated, m.u32(kFiguresUpdated) + 1);
    const uint8_t type = m.u8(kFigureType + r);
    m.put32(kTimerType, type);
    m.put32(kTimerStart, process.ticks());
    // Étape 3 : l'appel du gestionnaire (0x004CC957). Le code qui suit relit EAX seulement.
    cpu.set_reg(Cpu::Reg::EAX, r);
    cpu.set_reg(Cpu::Reg::ECX, static_cast<uint32_t>(figure));
    cpu.set_reg(Cpu::Reg::EDX, type);
    if ((m.u32(kFigureUpdateFlags + r) & 0x80000) != 0 && type != 0x2B) return kWalkersUpdateOther;
    return kWalkersUpdateHandler;
}

// En haut de la boucle (0x004CC8D5) : EDX est le numéro de la fiche.
uint32_t loop_top(Cpu& cpu, const Process& process) {
    if (*cpu.host(kTimerPrecise) != 0) return 0;
    return next_figure(cpu, process, static_cast<int32_t>(cpu.reg(Cpu::Reg::EDX)));
}

// À l'entrée de FUN_004cc760, appelée depuis la boucle.
uint32_t timer_stop(Cpu& cpu, const Process& process) {
    const Memory m{cpu.host(0)};
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    if (m.u8(kTimerPrecise) != 0 || m.u32(esp) != kWalkersUpdateAfterTimer) return 0;
    // Étape 4 : la minuterie.
    const uint32_t now = process.ticks();
    m.put32(kTimerStop, now);
    const uint32_t elapsed = now - m.u32(kTimerStart);
    const uint32_t slot = kTimerTotals + (m.u32(kTimerType) << 5);
    m.put32(slot, m.u32(slot) + 1);
    m.put32(slot + 4, m.u32(slot + 4) + elapsed);
    const uint32_t longest = m.u32(slot + 8);
    if (static_cast<int32_t>(elapsed) > static_cast<int32_t>(longest)) m.put32(slot + 8, elapsed);
    cpu.set_reg(Cpu::Reg::ESP, esp + 4);
    // Étape 5 laissée à l'original : il relit la fiche dans 0x00EF57EC.
    const int32_t figure = static_cast<int32_t>(m.u32(kFigureCurrent));
    if (m.u8(kFigurePlaced + record(figure)) == 2) {
        cpu.set_reg(Cpu::Reg::EAX, elapsed & 0xFFFFFF00u);  // « MOV AL,[0x00EF57FC] » : AL = 0
        cpu.set_reg(Cpu::Reg::ECX, m.u32(kTimerType) << 5);
        cpu.set_reg(Cpu::Reg::EDX, longest);
        return kWalkersUpdateAfterTimer;
    }
    return next_figure(cpu, process, figure + 1);
}

// --- Soldats ---------------------------------------------------------------------------------

uint32_t formation(int32_t index) { return static_cast<uint32_t>(index) * 0x9C; }
uint32_t building(int32_t index) { return static_cast<uint32_t>(index) * 0x118; }

// FUN_004200f0(0) : partie de Poséidon.
bool poseidon(const Memory& m) { return m.u32(kGameMode) == 1 && m.s16(kGameSubmode) == 1; }

// Image d'un identifiant positif : bits 9 et plus, fichier d'images ; bits 0 à 8, rang à partir de
// 1 dans le fichier (0 : pas d'image). Un rang au-delà du dernier lit la première entrée.
uint32_t image_of(const Memory& m, int32_t id) {
    const int32_t file = id >> 9, rank = id & 0x1FF;
    if (rank == 0) return 0;
    const uint32_t table = static_cast<uint32_t>(file) * kImageFileStride;
    uint32_t entry = m.u32(kImageGroups + table);
    if (rank - 1 <= m.i32(kImageLast + table)) entry += static_cast<uint32_t>(rank - 1) * 8;
    return (static_cast<uint32_t>(file) << 14) + m.u32(entry + 4);
}

// FUN_0053fb60(bâtiment) sans valeur de retour : la case de route par laquelle on entre dans un
// bâtiment composé (FUN_005466a0 donne la case d'origine de sa pièce principale, FUN_0053f810
// cherche la route autour). `scratch` : place libre sous la pile pour les variables de
// FUN_0053f810. Rien si FUN_0053f810 n'est pas traitée en natif.
std::optional<std::pair<int32_t, int32_t>> road_entry(Cpu& cpu, const Memory& m, int32_t number, uint32_t scratch) {
    const uint32_t b = building(number);
    const int32_t main = number - m.s16(kBuildings + 2 + b);
    const uint32_t origin = building(main - m.s16(kBuildings + 2 + building(main)));
    int32_t x = m.s16(kBuildings - 0x0A + origin), y = m.s16(kBuildings - 8 + origin);
    const uint32_t parts = composite_parts(cpu, kCompositeTables, m.s16(kBuildings + origin));
    if (parts) {
        int32_t dx, dy;
        composite_offset(cpu, parts, m.u8(kBuildings + 0x8C + origin), dx, dy);
        x -= dx;
        y -= dy;
    }
    const uint32_t out_x = scratch + 0x40, out_y = scratch + 0x44;
    if (!composite_road_entry(cpu, kCompositeTables, m.s16(kBuildings + b), x, y, m.u8(kBuildings + 0x8C + b),
                              out_x, out_y, scratch))
        return std::nullopt;
    return std::make_pair(m.i32(out_x), m.i32(out_y));
}

// Ce que FUN_004b1630 (soldat qui tient sa place) fait d'un soldat.
enum class Place {
    Halted,   // groupe arrêté (+0x90 et +0x93) : rien
    Removed,  // groupe sans bâtiment, ou bâtiment ni composé ni de 100 à 114 : fiche à retirer
    Linked,   // groupe rattaché (+0x94) à un meneur (+0x78) : rien d'autre
    Placed,   // le soldat est sur la case d'entrée du bâtiment : +0x5D à 1
};

std::optional<Place> place(Cpu& cpu, const Memory& m, uint32_t r, uint32_t g, uint32_t scratch) {
    if (m.u8(kFormationFlag90 + g) != 0 && m.u8(kFormationHalted + g) != 0) return Place::Halted;
    const int32_t number = m.s16(kFormationBuilding + g);
    if (number == 0) return Place::Removed;
    const int32_t type = m.s16(kBuildings + building(number));
    if (!composite_type(type) && (type < 100 || type > 0x72)) return Place::Removed;
    const auto entry = road_entry(cpu, m, number, scratch);
    if (!entry) return std::nullopt;
    if (m.u32(kFormationLinked + g) != 0 && m.s16(kFormationLeader + g) != -1) return Place::Linked;
    if (m.s16(kFigureX + r) == entry->first && m.s16(kFigureY + r) == entry->second) return Place::Placed;
    return std::nullopt;  // il rejoint sa place : FUN_004b11c0, laissée à l'original
}

// Ce qu'un soldat qui tient sa place dans son groupe (action 6) a en commun avec les autres, une
// fois la décision prise : prologue des gestionnaires (FUN_004e1b20(personnage, 8), dont seule
// l'écriture compte ici : +0x6A du groupe ramené à 10 ; case visée) et FUN_004b1630. Les hoplites
// et les cavaliers mettent tout le groupe sur sa case quand la disposition est 5, pas les
// porte-étendards. FUN_004b1630 remet le pas de l'animation à 0.
void hold_place(const Memory& m, uint32_t r, int32_t group, uint32_t g, Place placed, bool layout5) {
    m.put8(kFigureByte6A + r, 0);
    m.put32(kFigureExtraImage + r, 0);
    if (group != 0 && m.u8(kFormationFlag69 + g) == 0 && m.u8(kFormationValue6A + g) >= 0x0B)
        m.put8(kFormationValue6A + g, 0x0A);  // FUN_004e1b20
    m.put8(kFigureInFormation + r, placed == Place::Halted ? 0 : 1);
    m.put8(kFigureAtPlace + r, placed == Place::Placed || m.u8(kFormationFlag03 + g) != 0 ? 1 : 0);
    const int32_t layout = m.s16(kFormationLayout + g);
    if (layout5 && layout == 5) {
        m.put16(kFigureGoalX + r, static_cast<int16_t>(m.s16(kFormationX + g)));
        m.put16(kFigureGoalY + r, static_cast<int16_t>(m.s16(kFormationY + g)));
    } else {
        const uint32_t offset = kFormationOffsets +
                                (static_cast<uint32_t>(layout) * 8 + m.u8(kFigureRankInFormation + r)) * 8;
        m.put16(kFigureGoalX + r, static_cast<int16_t>(m.s16(offset) + m.s16(kFormationX + g)));
        m.put16(kFigureGoalY + r, static_cast<int16_t>(m.s16(offset + 4) + m.s16(kFormationY + g)));
    }
    // FUN_004b1630.
    m.put16(kFigureWait + r, 0);
    m.put8(kFigureFrame + r, 0);
    if (placed == Place::Removed) m.put8(kFigurePlaced + r, 2);
}

// Direction à l'écran (0x00FA05E4) : direction du personnage, ou de repli au-delà de 7, moins
// l'orientation de la vue.
int32_t view_direction(const Memory& m, uint32_t r) {
    int32_t direction = m.s8(kFigureDirection + r);
    if (direction >= 8) direction = m.s8(kFigureFallbackDirection + r);
    direction -= m.i32(kViewOrientation);
    if (direction < 0) direction += 8;
    if (direction >= 8) direction -= 8;
    m.put32(kFigureViewDirection, static_cast<uint32_t>(direction));
    return direction;
}

// Fiche (72 octets) d'une image donnée par son fichier (bits 14 et plus) et son rang (bits 0 à 13).
// Un rang au-delà du dernier lit la première fiche du fichier.
uint32_t image_record(const Memory& m, uint32_t image) {
    const uint32_t table = (image >> 14) * kImageFileStride, index = image & 0x3FFF;
    uint32_t entry = m.u32(kImageRecords + table);
    if (static_cast<int32_t>(index) <= m.i32(kImageRecordLast + table)) entry += index * 72;
    return entry;
}

// FUN_005af770 : image d'une animation (identifiant), au pas et dans la direction donnés, ramenés
// au dernier pas (+0x1E) et à la dernière direction (+0x20) de sa première image.
uint32_t animation_image(const Memory& m, int32_t id, uint8_t frame, int32_t direction) {
    const uint32_t first = image_of(m, id), entry = image_record(m, first);
    const int32_t frames = m.s16(entry + 0x1E), directions = m.s16(entry + 0x20);
    if (frame >= frames) frame = static_cast<uint8_t>(frames - 1);
    if (direction >= directions) direction = directions - 1;
    return frame * static_cast<uint32_t>(directions) + first + static_cast<uint32_t>(direction);
}

// FUN_005af8c0(identifiant, &pas, direction, 0) : avance le pas d'une animation, puis son image.
uint32_t animate(const Memory& m, int32_t id, uint8_t& frame, int32_t direction) {
    const int32_t frames = m.s16(image_record(m, image_of(m, id)) + 0x1E);
    if (frame >= frames) frame = 0;
    if (++frame >= frames) frame = 0;
    return animation_image(m, id, frame, direction);
}

// FUN_004e24a0 : le premier membre du groupe qui n'est pas en action 4 (0 s'il n'y en a pas), ou
// le personnage lui-même s'il est sans groupe ou si le groupe a +0x69.
int32_t formation_head(const Memory& m, int32_t figure, int32_t group, uint32_t g) {
    if (group == 0 || m.u8(kFormationFlag69 + g) != 0) return figure;
    for (uint32_t i = 0; i < 8; ++i) {
        const int32_t member = m.s16(kFormationMembers + g + 2 * i);
        if (member > 0 && m.u8(kFigureAction + record(member)) != 4) return member;
    }
    return 0;
}

// FUN_004b31f0, gestionnaire des hoplites (type 0x0B), quand l'hoplite tient sa place dans son
// groupe (action 6) : prologue commun aux soldats, FUN_004b17f0 (rien en action 6), FUN_004b1630,
// direction et image. Les autres actions, et l'hoplite qui doit rejoindre sa place, sont laissés
// à l'original.
std::optional<uint32_t> hoplite(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    const int32_t figure = m.i32(kFigureCurrent);
    const uint32_t r = record(figure);
    const int32_t group = m.s16(kFigureFormation + r);
    const uint32_t g = formation(group);
    // Action 6 après le prologue (4 si le groupe n'est pas en service) et FUN_004b17f0.
    if (m.u8(kFigureAction + r) != 6 || m.u8(kFormations + g) != 1) return std::nullopt;
    if (group == 0x1F || group == 0x20) return std::nullopt;  // 0x004B389E : autres images
    const std::optional<Place> placed = place(cpu, m, r, g, cpu.reg(Cpu::Reg::ESP) - 0x200);
    if (!placed) return std::nullopt;

    // Le pas de l'animation, avancé par le prologue puis remis à 0 par FUN_004b1630, reprend sa
    // valeur d'entrée en 0x004B3662.
    const uint8_t frame = m.u8(kFigureFrame + r);
    hold_place(m, r, group, g, *placed, true);
    // 0x004B364B : direction du groupe.
    m.put8(kFigureFallbackDirection + r, m.u8(kFormationDirection + g));
    m.put8(kFigureFrame + r, frame);
    const int32_t direction = view_direction(m, r);
    const uint32_t image = image_of(m, poseidon(m) ? 0x6A4C : 0x667E) + frame * 8u + static_cast<uint32_t>(direction);
    m.put32(kFigureImage + r, image);
    return image;
}

// FUN_004b26b0, gestionnaire des cavaliers (type 0x0C), dans le même cas que les hoplites, pour
// les groupes 0x21 et 0x22 (les seuls des scénarios ; les autres ont d'autres images, 0x004B2CE2).
// En plus : FUN_004e17a0 (groupe sans cible, +0x82 négatif), la direction du groupe prise sur son
// premier membre (FUN_004e2480), les images chargées (FUN_004ffb70(10)) et une animation
// (FUN_005af8c0) dont le pas est une copie, à 0 depuis FUN_004b1630.
std::optional<uint32_t> horseman(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    const int32_t figure = m.i32(kFigureCurrent);
    const uint32_t r = record(figure);
    const int32_t group = m.s16(kFigureFormation + r);
    const uint32_t g = formation(group);
    if (m.u8(kFigureAction + r) != 6 || m.u8(kFormations + g) != 1) return std::nullopt;
    if (group != 0x21 && group != 0x22) return std::nullopt;
    if (m.s16(kFormationTarget + g) >= 0) return std::nullopt;       // FUN_004e17a0 : action 0x0F
    if (m.s16(kFigureLinkedBack + r) != 0) return std::nullopt;      // 0x004B2995
    if (m.u8(kSpritesLoaded + 0x0A) == 0) return std::nullopt;       // FUN_004ffb70 les charge
    const std::optional<Place> placed = place(cpu, m, r, g, cpu.reg(Cpu::Reg::ESP) - 0x200);
    if (!placed) return std::nullopt;

    hold_place(m, r, group, g, *placed, true);
    if (formation_head(m, figure, group, g) == figure)  // FUN_004e2480
        m.put8(kFormationDirection + g, m.u8(kFigureDirection + r));
    const int32_t direction = view_direction(m, r);
    uint8_t frame = 0;
    const uint32_t image = animate(m, 0x6401, frame, direction);
    m.put32(kFigureImage + r, image);
    return image;
}

// FUN_004b1840, gestionnaire des porte-étendards (type 0x0D), dans le même cas que les hoplites ;
// FUN_004e17a0 comme pour les cavaliers. Le pas de l'animation reprend sa valeur d'entrée
// (0x004B2298).
std::optional<uint32_t> standard_bearer(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    const int32_t figure = m.i32(kFigureCurrent);
    const uint32_t r = record(figure);
    const int32_t group = m.s16(kFigureFormation + r);
    const uint32_t g = formation(group);
    if (m.u8(kFigureAction + r) != 6 || m.u8(kFormations + g) != 1) return std::nullopt;
    if (m.s16(kFormationTarget + g) >= 0) return std::nullopt;       // FUN_004e17a0 : action 0x0F
    const std::optional<Place> placed = place(cpu, m, r, g, cpu.reg(Cpu::Reg::ESP) - 0x200);
    if (!placed) return std::nullopt;

    const uint8_t frame = m.u8(kFigureFrame + r);
    hold_place(m, r, group, g, *placed, false);
    m.put8(kFigureFrame + r, frame);
    const int32_t direction = view_direction(m, r);
    const uint32_t image = image_of(m, poseidon(m) ? 0x6A49 : 0x6685) + frame * 8u + static_cast<uint32_t>(direction);
    m.put32(kFigureImage + r, image);
    return image;
}

// --- Déplacement ------------------------------------------------------------------------------

// FUN_004d94f0 : un pas dans la case. La position fine (+0x42, +0x44) avance d'un cran dans la
// direction (+9 ; au-delà de 7, rien). Pendant un saut (+0x6F, décompté à chaque pas), la hauteur
// (+0x70) monte vers son plafond (+0x71), deux fois sur une case d'eau ou de rivage
// (0x10000400) ; à la fin du saut, +0x5D et le plan +0x15 reviennent à 0. Hors saut, la hauteur
// redescend d'un cran.
void step_in_tile(const Memory& m, uint32_t r) {
    const uint32_t x = kFigureFineX + r, y = kFigureFineY + r;
    const auto add = [&m](uint32_t a, int v) { m.put16(a, static_cast<int16_t>(m.s16(a) + v)); };
    switch (m.u8(kFigureDirection + r)) {
        case 0: add(y, -1); break;
        case 1: add(y, -1); add(x, 1); break;
        case 2: add(x, 1); break;
        case 3: add(y, 1); add(x, 1); break;
        case 4: add(y, 1); break;
        case 5: add(y, 1); add(x, -1); break;
        case 6: add(x, -1); break;
        case 7: add(y, -1); add(x, -1); break;
        default: break;
    }
    const uint8_t jump = m.u8(kFigureJump + r);
    if (jump == 0) {
        if (m.u8(kFigureLift + r) != 0) m.put8(kFigureLift + r, m.u8(kFigureLift + r) - 1);
        return;
    }
    m.put8(kFigureJump + r, jump - 1);
    if (jump == 1) {
        m.put8(kFigureAtPlace + r, 0);
        m.put8(kFigureLayer + r, 0);
        return;
    }
    const auto rise = [&m, r] {
        const uint8_t top = m.u8(kFigureLiftTop + r);
        if (m.u8(kFigureLift + r) < top) m.put8(kFigureLift + r, m.u8(kFigureLift + r) + 1);
        if (m.u8(kFigureLift + r) > top) m.put8(kFigureLift + r, m.u8(kFigureLift + r) - 1);
    };
    rise();
    if (m.u32(kTileFlags + m.u32(kFigureTile + r) * 4) & 0x10000400) rise();
}

// FUN_004d1170(personnage, allure), vitesses 5 (trois pas sur quatre, compteur +0x158) et 6 (un pas
// par tour), quand le pas reste dans la case : FUN_004d7bd0(personnage, 1) avance le pas (+0x31)
// puis appelle FUN_004d94f0. Laissés à l'original : les autres allures, le personnage sans chemin
// (+0x32 nul ou négatif : FUN_004d8220 le cherche), l'arrivée sur une case (pas 15 :
// FUN_004dfb70, FUN_004d8b10, FUN_004d8bb0) et le milieu de la case (pas 8 : FUN_004d9330).
std::optional<uint32_t> move_figure(Cpu& cpu) {
    const Memory m{cpu.host(0)};
    const int32_t figure = static_cast<int32_t>(cpu.arg(0));
    const uint32_t speed = cpu.arg(1), r = record(figure);
    if (speed != 5 && speed != 6) return std::nullopt;
    if (speed == 5 && m.u8(kFigureSpeedCount + r) > 2) {  // 0x004D14C4 : tour sans pas
        m.put8(kFigureSpeedCount + r, 0);
        return static_cast<uint32_t>(figure);
    }
    if (m.s16(kFigurePathLeft + r) <= 0) return std::nullopt;
    const int8_t step = static_cast<int8_t>(m.u8(kFigureStep + r) + 1);
    if (step >= 15 || step == 8) return std::nullopt;

    m.put8(kFigureStep + r, static_cast<uint8_t>(step));
    step_in_tile(m, r);
    m.put32(kFigureStepFlag, 0);
    if (speed == 6) return r;
    const uint8_t count = m.u8(kFigureSpeedCount + r) + 1;
    m.put8(kFigureSpeedCount + r, count);
    return (r & 0xFFFFFF00u) | count;
}

template <size_t N>
uint64_t code_fingerprint(Cpu& cpu, const uint32_t (&ranges)[N][2]) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& range : ranges)
        for (uint32_t a = range[0]; a < range[1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

// Code réécrit ici : walkers_update et ses deux minuteries.
constexpr uint32_t kRewritten[][2] = {
    {kTimerStartFunction, kTimerStartFunctionEnd},
    {kTimerStopFunction, kTimerStopFunctionEnd},
    {kWalkersUpdate, kWalkersUpdateFunctionEnd},
};
constexpr uint64_t kRewrittenHash = 0x5D9AB90D7692C378ull;

// Code des soldats lu ou réécrit ici, avec les tables de sauts.
constexpr uint32_t kSoldiers[][2] = {
    {kPoseidonGame, kPoseidonGameEnd},           {kSoldierPlace, kSoldierPlaceEnd},
    {kSoldierRegroup, kSoldierRegroupEnd},       {kHoplite, kHopliteEnd},
    {kFigureTypeValue, kFigureTypeValueEnd},     {kBuildingRoadEntry, kBuildingRoadEntryEnd},
    {kBuildingOrigin, kBuildingOriginEnd},
};
constexpr uint64_t kSoldiersHash = 0x29B28F1E308EF3AFull;

// En plus, pour les cavaliers et les porte-étendards.
constexpr uint32_t kMounted[][2] = {
    {kHorseman, kHorsemanEnd},           {kStandardBearer, kStandardBearerEnd},
    {kSoldierEngaged, kSoldierEngagedEnd}, {kFormationHead, kFormationHeadEnd},
    {kLoadSprites, kLoadSpritesEnd},     {kAnimationImage, kAnimationImageEnd},
};
constexpr uint64_t kMountedHash = 0x01032A8C17B6C6FAull;

// Déplacement : FUN_004d1170 et sa table de sauts, FUN_004d7bd0, FUN_004d94f0 et sa table.
constexpr uint32_t kMovement[][2] = {
    {kMoveFigure, kMoveFigureEnd}, {kMoveSteps, kMoveStepsEnd}, {kStepInTile, kStepInTileEnd},
};
constexpr uint64_t kMovementHash = 0x288AF3446813FC18ull;

}  // namespace

int install_native_figures(Process& p) {
    Cpu& cpu = p.cpu();
    if (code_fingerprint(cpu, kRewritten) != kRewrittenHash) return 0;
    // Octets déplacés, sans adresse relative : « LEA EAX,[EDX+EDX*2] ; SHL EAX,5 » (6) et
    // « MOV AL,[0x00EF57FC] ; SUB ESP,8 » (8).
    cpu.add_code_jump(kWalkersUpdateLoop, 6, [&p](Cpu& c) { return loop_top(c, p); });
    cpu.add_code_jump(kTimerStopFunction, 8, [&p](Cpu& c) { return timer_stop(c, p); });
    int installed = 2;
    if (composites_known() && code_fingerprint(cpu, kSoldiers) == kSoldiersHash) {
        // « SUB ESP,0xC ; MOV EDX,[0x00EF57EC] » déplacés.
        cpu.add_fast_path(kHoplite, 0, 9, hoplite);
        ++installed;
        if (code_fingerprint(cpu, kMounted) == kMountedHash) {
            // « SUB ESP,8 ; MOV EAX,[0x00EF57EC] » et « SUB ESP,0xC ; MOV EAX,[0x00EF57EC] » déplacés.
            cpu.add_fast_path(kHorseman, 0, 8, horseman);
            cpu.add_fast_path(kStandardBearer, 0, 8, standard_bearer);
            installed += 2;
        }
    }
    if (code_fingerprint(cpu, kMovement) == kMovementHash) {
        // « MOV EAX,[ESP+8] ; PUSH ESI » déplacés.
        cpu.add_fast_path(kMoveFigure, 0, 5, move_figure);
        ++installed;
    }
    return installed;
}

}  // namespace zeus::win32
