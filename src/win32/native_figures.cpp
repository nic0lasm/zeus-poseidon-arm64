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
// Les indices et adresses sont calculés en 32 bits comme dans l'original. Adresses :
// win32/game_gog.h. Le code n'est modifié que si ses octets sont ceux attendus (empreinte FNV-1a,
// comparée avant toute modification).

#include <cstdint>
#include <cstring>

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

// Code réécrit ici : walkers_update et ses deux minuteries.
constexpr uint32_t kRewritten[][2] = {
    {kTimerStartFunction, kTimerStartFunctionEnd},
    {kTimerStopFunction, kTimerStopFunctionEnd},
    {kWalkersUpdate, kWalkersUpdateFunctionEnd},
};
constexpr uint64_t kRewrittenHash = 0x5D9AB90D7692C378ull;

uint64_t fingerprint(Cpu& cpu) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& range : kRewritten)
        for (uint32_t a = range[0]; a < range[1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_figures(Process& p) {
    Cpu& cpu = p.cpu();
    if (fingerprint(cpu) != kRewrittenHash) return 0;
    // Octets déplacés, sans adresse relative : « LEA EAX,[EDX+EDX*2] ; SHL EAX,5 » (6) et
    // « MOV AL,[0x00EF57FC] ; SUB ESP,8 » (8).
    cpu.add_code_jump(kWalkersUpdateLoop, 6, [&p](Cpu& c) { return loop_top(c, p); });
    cpu.add_code_jump(kTimerStopFunction, 8, [&p](Cpu& c) { return timer_stop(c, p); });
    return 2;
}

}  // namespace zeus::win32
