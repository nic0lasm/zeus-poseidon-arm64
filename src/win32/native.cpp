// Fonctions du jeu réécrites pour l'hôte.
//
// Le profil d'exécution (zeus-run --profile) montre qu'une cinquantaine de fonctions font
// presque tout le temps d'exécution. Celles qui figurent ici sont réécrites à la main et
// exécutées à la place du code d'origine ; le reste du jeu reste émulé. Chaque réécriture doit
// laisser le journal d'images d'une exécution --deterministic strictement identique.
//
// Adresses de l'exécutable GOG (SHA-256 382bcd28…). Une fonction n'est remplacée que si ses
// premiers octets sont ceux attendus.

#include <cstdio>
#include <cstring>

#include "win32/process.h"

namespace zeus::win32 {
namespace {

uint32_t u32(Cpu& cpu, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, cpu.host(address), 4);
    return value;
}

// FUN_00413dc0(source) : copie un rectangle d'image opaque vers le tampon d'affichage.
// Destination : tampon 0xF9C100, ligne 0xF2A698 (de 0xF13A3C octets), colonne 0xF13A40.
// 0xF13A34 lignes de 0xF139A4 pixels de 16 bits ; après chaque ligne la source avance en plus
// de 0xF139A0 octets et la destination de 0xF1399C.
std::optional<uint32_t> copy_rectangle(Cpu& cpu) {
    const uint8_t* source = cpu.host(cpu.arg(0));
    uint8_t* target = cpu.host(u32(cpu, 0xF9C100) + u32(cpu, 0xF2A698) * u32(cpu, 0xF13A3C) + u32(cpu, 0xF13A40) * 2);
    const int32_t rows = static_cast<int32_t>(u32(cpu, 0xF13A34));
    const uint32_t row_bytes = u32(cpu, 0xF139A4) * 2;
    const uint32_t source_skip = u32(cpu, 0xF139A0), target_skip = u32(cpu, 0xF1399C);
    for (int32_t row = 0; row < rows; ++row) {
        std::memmove(target, source, row_bytes);
        source += row_bytes + source_skip;
        target += row_bytes + target_skip;
    }
    return 0u;
}

// __strcmpi de la bibliothèque C de Visual C++ 6, hors paramètres régionaux (0x160BFA4 nul) :
// comparaison sans tenir compte de la casse des lettres ASCII ; résultat -1, 0 ou 1.
std::optional<uint32_t> strcmpi(Cpu& cpu) {
    if (u32(cpu, 0x160BFA4) != 0) return std::nullopt;
    const uint8_t* a = cpu.host(cpu.arg(0));
    const uint8_t* b = cpu.host(cpu.arg(1));
    auto lower = [](uint8_t c) { return c >= 'A' && c <= 'Z' ? static_cast<uint8_t>(c + 0x20) : c; };
    for (;; ++a, ++b) {
        const uint8_t x = lower(*a), y = lower(*b);
        if (x != y) return x < y ? 0xFFFFFFFFu : 1u;
        if (!*b) return 0u;
    }
}

struct Replacement {
    uint32_t address;
    uint8_t first_bytes[5];
    uint32_t arg_bytes;
    std::optional<uint32_t> (*fn)(Cpu&);
};

constexpr Replacement kReplacements[] = {
    {0x00413DC0, {0xA1, 0x98, 0xA6, 0xF2, 0x00}, 0, copy_rectangle},
    {0x005CC640, {0x55, 0x8B, 0xEC, 0x57, 0x56}, 0, strcmpi},
};

}  // namespace

void install_native_functions(Process& p) {
    Cpu& cpu = p.cpu();
    int installed = 0;
    for (const Replacement& r : kReplacements) {
        if (std::memcmp(cpu.host(r.address), r.first_bytes, sizeof r.first_bytes) != 0) continue;
        cpu.replace_function(r.address, r.arg_bytes, r.fn);
        ++installed;
    }
    if (p.trace) std::fprintf(stderr, "[hôte] %d fonctions du jeu remplacées par des fonctions natives\n", installed);
}

}  // namespace zeus::win32
