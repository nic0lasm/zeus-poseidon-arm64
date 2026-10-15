// Banc d'équivalence (voir equivalence.h et docs/equivalence.md).

#include "win32/equivalence.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "win32/game_gog.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

// Zones de l'état de la partie. Tous les blocs qu'écrit FUN_00510ed0, qui enregistre la partie
// (un appel de FUN_0050cb80 par bloc : 139 pour une sauvegarde), sauf les trois premiers, qui
// décrivent le fichier lui-même (version, nombre de blocs, table). Relevés dans la table d'une
// sauvegarde écrite par Zeus.exe (GOG) sous zeus-run : rang, adresse, octets (taille d'un élément
// × nombre). Une variable enregistrée plusieurs fois (0x00F9C148) l'est aussi ici.
constexpr StateZone kZones[] = {
    {3, 0x00F2A7B2, 1}, {4, 0x00FE6A58, 19184}, {5, 0x0091EC04, 4}, {6, 0x0091EC08, 4},
    {7, 0x008FBAC0, 12584}, {8, 0x0065B9B0, 144}, {9, 0x00902928, 2032}, {10, 0x008FEBE8, 14400},
    {11, 0x006A9C9C, 1}, {12, 0x00F2A988, 4}, {13, 0x009173F4, 18600}, {14, 0x009173F0, 4},
    {15, 0x00D404B8, 51984}, {16, 0x00D26E98, 103968}, {17, 0x00CCE128, 207936}, {18, 0x00CC1618, 51984},
    {19, 0x00D0D878, 103968}, {20, 0x00D00D68, 51984}, {21, 0x00CB4B08, 51984}, {22, 0x00CA7FF8, 51984},
    {23, 0x00C9B4E8, 51984}, {24, 0x00C8E9D8, 51984}, {25, 0x00C753B8, 103968}, {26, 0x00B90C98, 51984},
    {27, 0x00B84188, 51984}, {28, 0x00DBD788, 776000}, {29, 0x00EF4FE8, 2000}, {30, 0x00E7AEC8, 500000},
    {31, 0x00DB7B40, 15600}, {32, 0x00F9C148, 4}, {33, 0x00FE6910, 1}, {34, 0x00FE6A14, 64},
    {35, 0x0092E658, 1120000}, {36, 0x00FA05E8, 4}, {37, 0x0091EC14, 4}, {38, 0x0091EC0C, 4},
    {39, 0x0091EC10, 4}, {40, 0x0091EBF4, 4}, {41, 0x00F9C148, 4}, {42, 0x00F137E4, 4},
    {43, 0x00F137E0, 4}, {44, 0x00DA2F64, 4}, {45, 0x00DA2F68, 4}, {46, 0x00665E30, 4},
    {47, 0x00FE69E8, 4}, {48, 0x008F9838, 4}, {49, 0x008F9834, 4}, {50, 0x00903120, 4},
    {51, 0x0091EB58, 144}, {52, 0x0091EAC8, 144}, {53, 0x0091EC00, 4}, {54, 0x00FD3838, 60000},
    {55, 0x00FD37F4, 4}, {56, 0x00FD37F0, 4}, {57, 0x00FD37EC, 4}, {58, 0x00FD37C4, 10},
    {59, 0x00FD3774, 80}, {60, 0x00FD3724, 80}, {61, 0x0091EC44, 4}, {62, 0x0091EC40, 4},
    {63, 0x00EF57F0, 4}, {64, 0x00F9C148, 4}, {65, 0x00F2A7C0, 4}, {66, 0x00F2A7C4, 4},
    {67, 0x007773D0, 8960}, {68, 0x0091EBF8, 4}, {69, 0x008F9860, 8800}, {70, 0x008F9858, 4},
    {71, 0x0091F1D0, 1000}, {72, 0x0091EDE8, 1000}, {73, 0x0091F5B8, 8000}, {74, 0x0091EC70, 4},
    {75, 0x0091EC68, 4}, {76, 0x00EF57D4, 4}, {77, 0x00EF57D0, 4}, {78, 0x00EF57CC, 4},
    {79, 0x00EF57C8, 4}, {80, 0x00EF57C4, 4}, {81, 0x009214F8, 53600}, {82, 0x00DB7B30, 4},
    {83, 0x0091EBFC, 4}, {84, 0x0102D1CC, 4}, {85, 0x0102D1C8, 4}, {86, 0x0102D1C4, 4},
    {87, 0x0102D1C0, 4}, {88, 0x00DA2FB8, 4}, {89, 0x00DA2FBC, 4}, {90, 0x00DA2FC0, 4},
    {91, 0x00DA2FC4, 4}, {92, 0x008F9828, 2}, {93, 0x0091EA44, 4}, {94, 0x0091EA40, 4},
    {95, 0x00F2A258, 65}, {96, 0x00FA05C0, 32}, {97, 0x00DA23A8, 4}, {98, 0x00DA23A4, 4},
    {99, 0x00C35C68, 51984}, {100, 0x00EF57D8, 16}, {101, 0x009034F8, 75168}, {102, 0x00C1C648, 51984},
    {103, 0x00B1E8D8, 32}, {104, 0x00699570, 36}, {105, 0x00B1E8F8, 207936}, {106, 0x006A9CBC, 26},
    {107, 0x0069DFC8, 13}, {108, 0x00C688A8, 51984}, {109, 0x00C29158, 51984}, {110, 0x00B1E8B8, 4},
    {111, 0x008E01FA, 51984}, {112, 0x008ECD0A, 51984}, {113, 0x00EF5800, 1468}, {114, 0x0090311C, 4},
    {115, 0x0078C578, 224}, {116, 0x00665CC8, 75}, {117, 0x00FE690C, 4}, {118, 0x00FE6908, 1},
    {119, 0x00781A74, 3600}, {120, 0x00EF5DD8, 728}, {121, 0x00DB68C0, 256}, {122, 0x00FFA450, 300},
    {123, 0x00FE43F0, 576}, {124, 0x00F0E4E8, 224}, {125, 0x00916AF8, 200}, {126, 0x00EF60B0, 221},
    {127, 0x00FE50D4, 17}, {128, 0x00FE2770, 456}, {129, 0x00FE276C, 4}, {130, 0x00665C7C, 75},
    {131, 0x0063A888, 4}, {132, 0x00665E48, 448}, {133, 0x007796D0, 4}, {134, 0x007796D4, 32000},
    {135, 0x00F135E8, 4}, {136, 0x00F13480, 360}, {137, 0x00F2A990, 1}, {138, 0x006AA4E0, 4},
    // Hors sauvegarde : FUN_00513a30 range à chaque tour le dernier tirage dans un historique de
    // 100 valeurs (indice 0x00F13624, valeurs dès 0x00F13628), avance les deux registres du
    // générateur (0x00F137E0, 0x00F137E4, blocs 43 et 42) et en tire six valeurs (0x00F137C4 à
    // 0x00F137D8). La zone couvre tout, de 0x00F13624 à 0x00F137E8.
    {-1, 0x00F13624, 0x1C4},
    // Hors sauvegarde, recalculées au chargement (formats/sav.md) : décalage des indices de case
    // (0x00DA2F80), bornes de la zone jouable (0x00DA2F78, 0x00DA2F7C), orientation de la vue
    // (0x00DA2F88) ; dernière réserve occupée du bloc 81 (0x0091EBF0) ; liste des numéros de
    // personnage libres (0x00DBB84C, reconstruite par FUN_004d9f40), dont la taille, jusqu'au bloc
    // 28, est une hypothèse.
    {-1, 0x00DA2F78, 0x14}, {-1, 0x0091EBF0, 4}, {-1, 0x00DBB84C, 0x1F3C},
};

// Noms des zones connues (game_gog.h, docs/reverse-engineering.md).
struct ZoneName {
    uint32_t address;
    const char* name;
};
constexpr ZoneName kZoneNames[] = {
    {kTileBits, "carte : bits de dessin"},          {kTileBuilding, "carte : bâtiment"},
    {kTileFlags, "carte : terrain"},                {kTileFigure, "carte : premier personnage"},
    {kTileSlope, "carte : pente"},                  {kTileHeight, "carte : hauteur"},
    {kFigures - 8, "personnages (2000 fiches)"},    {kBuildings - 0x10, "bâtiments (4000 fiches)"},
    {0x00F137E4, "générateur aléatoire (registre)"}, {0x00F137E0, "générateur aléatoire (registre)"},
    {0x00F13624, "générateur aléatoire (tour)"},  {0x00DA2F78, "carte : décalage, bornes, orientation"},
    {0x0091EBF0, "dernière réserve occupée"},        {0x00DBB84C, "numéros de personnage libres"},
};

// Empreinte 64 bits d'une zone : FNV-1a sur des mots de 8 octets, avec un brassage des bits hauts
// vers les bits bas à chaque mot.
uint64_t hash_bytes(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t word;
        std::memcpy(&word, p + i, 8);
        h = (h ^ word) * 0x100000001B3ull;
        h ^= h >> 32;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

}  // namespace

const StateZone* state_zones(size_t& count) {
    count = sizeof kZones / sizeof kZones[0];
    return kZones;
}

std::string zone_name(const StateZone& zone) {
    for (const ZoneName& n : kZoneNames)
        if (n.address == zone.address) return n.name;
    return zone.block >= 0 ? "bloc " + std::to_string(zone.block) : "hors sauvegarde";
}

// --- Empreinte d'état par tour --------------------------------------------------------------

class StateLog {
public:
    StateLog(Process& process, const std::string& path) : process_(process), out_(std::fopen(path.c_str(), "w")) {
        if (!out_) throw std::runtime_error("--state-log : impossible d'écrire " + path);
        // En-tête : une ligne par zone, dans l'ordre des colonnes.
        for (const StateZone& z : kZones)
            std::fprintf(out_, "# zone %d 0x%08X %u %s\n", z.block, z.address, z.size, zone_name(z).c_str());
    }
    ~StateLog() {
        if (out_) std::fclose(out_);
    }
    StateLog(const StateLog&) = delete;
    StateLog& operator=(const StateLog&) = delete;

    void line(const char* label) {
        if (!out_) return;
        std::fputs(label, out_);
        for (const StateZone& z : kZones)
            std::fprintf(out_, " %016" PRIx64, hash_bytes(process_.cpu().host(z.address), z.size));
        std::fputc('\n', out_);
    }
    void tick() {
        line(std::to_string(ticks_++).c_str());
    }
    void finish() {
        line("fin");
        std::fclose(out_);
        out_ = nullptr;
    }

private:
    Process& process_;
    std::FILE* out_;
    uint64_t ticks_ = 0;
};

std::shared_ptr<StateLog> install_state_log(Process& process, const std::string& path) {
    auto log = std::make_shared<StateLog>(process, path);
    // À l'entrée du tour : l'état laissé par le tour précédent et par tout ce qui s'est passé
    // depuis (ordres du joueur, chargement).
    process.cpu().add_code_hook(kSimulationTick, [log](Cpu&) { log->tick(); });
    return log;
}

void finish_state_log(StateLog& log) { log.finish(); }

// --- Vérification ciblée --------------------------------------------------------------------

namespace {

enum : uint32_t { kEax = 1, kEcx = 2, kEdx = 4, kEbx = 8, kEsp = 16, kEbp = 32, kEsi = 64, kEdi = 128 };
constexpr Cpu::Reg kRegs[] = {Cpu::Reg::EAX, Cpu::Reg::ECX, Cpu::Reg::EDX, Cpu::Reg::EBX,
                              Cpu::Reg::ESP, Cpu::Reg::EBP, Cpu::Reg::ESI, Cpu::Reg::EDI};
constexpr const char* kRegNames[] = {"EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"};

// Ce qui ne fait pas partie du résultat d'une fonction connue.
//   - Registres : ECX et EDX ne sont jamais relus par l'appelant (registres libres de toutes les
//     conventions d'appel de Visual C++), ni EAX quand la fonction ne renvoie rien (signature de
//     Ghidra) ou que ses appelants ne lisent pas sa valeur. Pour un morceau de code, le
//     commentaire du remplacement donne la preuve. Les indicateurs ne sont jamais comparés.
//   - Arguments : la fonction d'origine réutilise ses emplacements d'arguments sur la pile comme
//     variables (Visual C++ le fait) ; l'appelant les dépile sans les relire. `args` octets à partir
//     de ESP + 4 à l'entrée ne sont donc pas comparés.
// Une adresse absente : ECX et EDX pour une fonction, aucun registre pour un morceau de code, et
// tous les arguments comparés.
struct Known {
    uint32_t start, end, ignored, args;
};
constexpr Known kKnown[] = {
    {kCopyRectangle, 0, kEax | kEcx | kEdx, 4},       // void FUN_00413dc0(source)
    {kDrawRunsBlended, 0, kEax | kEcx | kEdx, 8},     // void FUN_00413e30(image, table)
    {kCopyRectangleKeyed, 0, kEax | kEcx | kEdx, 4},  // void FUN_00416010(source)
    {kDrawRuns, 0, kEax | kEcx | kEdx, 8},            // void FUN_004161b0(image, ombre)
    {kClearFrame, 0, kEax | kEcx | kEdx, 0},          // void FUN_0050b410(void)
    {kCityDraw, 0, kEax | kEcx | kEdx, 4},            // void FUN_00458170(mode)
    {kStrcmpi, 0, kEcx | kEdx, 8},                    // int _strcmpi(a, b)
    {kSpriteDispatch, 0, kEcx | kEdx, 0},             // stdcall : ses arguments sont dépilés par elle
    // FUN_00457900(case, x, y, rangée impaire) : ses deux appelants réécrivent EAX avant de le lire
    // (0x0045864A, puis 0x00458653 ; 0x00458C33, puis 0x00458C48, 0x00458CFF ou 0x00458D8A).
    {kGroundTile, 0, kEax | kEcx | kEdx, 16},
    // native.cpp, search_key : EAX, ECX et les indicateurs sont réécrits avant d'être relus.
    {kKeySearch, kKeySearchEnd, kEax | kEcx, 0},
};

struct Range {
    uint32_t start, end;  // [start, end[
    size_t offset;        // place dans les copies
};

}  // namespace

class Verifier {
public:
    Verifier(Process& process, uint32_t start, uint32_t end, uint32_t every)
        : process_(process), cpu_(process.cpu()), start_(start), end_(end), every_(std::max<uint32_t>(every, 1)) {
        ignored_ = end ? 0 : kEcx | kEdx;
        for (const Known& k : kKnown)
            if (k.start == start && k.end == end) {
                ignored_ = k.ignored;
                args_bytes_ = k.args;
            }
        cpu_.add_code_hook(start_, [this](Cpu&) { enter(); });
        if (end_)
            cpu_.add_code_hook(end_, [this](Cpu& c) {
                if (phase_ != Phase::Idle && c.reg(Cpu::Reg::ESP) == entry_esp_) leave();
            });
        else
            // Adresse de retour substituée à celle de l'appelant pendant les deux exécutions.
            return_thunk_ = cpu_.add_host_function({"<banc d'équivalence>", 0, [this](Cpu&) {
                                                        leave();
                                                        return 0u;
                                                    }});
    }

    bool report() {
        // Partie terminée (--seconds, --frames) au milieu d'un passage vérifié.
        if (phase_ == Phase::Original || phase_ == Phase::Native)
            failure_ = "passage " + std::to_string(calls_) + " inachevé à la fin de l'exécution\n";
        char what[48];
        if (end_) std::snprintf(what, sizeof what, "%#010x:%#010x", start_, end_);
        else std::snprintf(what, sizeof what, "%#010x", start_);
        std::fprintf(stderr, "vérification %s : %llu passages, %llu comparés (1 sur %u), %llu laissés à l'original (appels de l'hôte), %s\n",
                     what, static_cast<unsigned long long>(calls_), static_cast<unsigned long long>(compared_), every_,
                     static_cast<unsigned long long>(skipped_),
                     failure_.empty() ? (compared_ ? "tous identiques" : "aucun") : "DIFFÉRENCE");
        if (!failure_.empty()) std::fprintf(stderr, "%s", failure_.c_str());
        return failure_.empty();
    }

private:
    enum class Phase { Idle, Original, Native, Failed };

    void enter() {
        if (restarting_) {  // reprise voulue par start_run
            restarting_ = false;
            return;
        }
        // DÉBUT atteint de nouveau sans appel plus profond : le morceau de code en est sorti sans
        // passer par FIN, ou la fonction a été quittée autrement que par son retour.
        if ((phase_ == Phase::Original || phase_ == Phase::Native) && cpu_.reg(Cpu::Reg::ESP) >= entry_esp_) {
            fail(std::string(phase_ == Phase::Original ? "l'original" : "la version native") +
                 " repasse par le début sans être arrivé à la fin de la zone vérifiée\n");
            return;
        }
        if (phase_ != Phase::Idle || calls_++ % every_ != 0) return;
        cpu_.save_context(entry_);
        entry_esp_ = cpu_.reg(Cpu::Reg::ESP);
        caller_ = cpu_.read_u32(entry_esp_);
        for (int i = 0; i < 6; ++i) args_[i] = cpu_.read_u32(entry_esp_ + 4 + 4 * static_cast<uint32_t>(i));
        collect_ranges();
        copy_out(before_);
        start_run(false);
    }

    // Exécution depuis l'état d'entrée, remplacements natifs coupés (l'original) ou actifs.
    void start_run(bool native) {
        if (!end_) {
            std::memcpy(cpu_.host(entry_esp_), &return_thunk_, 4);
            return_address_ = caller_;
        }
        host_calls_ = cpu_.host_calls();
        cpu_.set_native(native);
        phase_ = native ? Phase::Native : Phase::Original;
        restarting_ = true;
        cpu_.jump(start_);
    }

    void leave() {
        // L'appel de la fonction de retour compte parmi les appels de l'hôte.
        const uint64_t host_calls = cpu_.host_calls() - host_calls_ - (end_ ? 0 : 1);
        uint32_t regs[8];
        for (int i = 0; i < 8; ++i) regs[i] = cpu_.reg(kRegs[i]);
        if (phase_ == Phase::Original) {
            if (host_calls) {
                // Les effets d'une fonction de l'hôte (horloge, fichiers, tas) ne se rejouent pas à
                // l'identique : l'appel garde le résultat de l'original, sans comparaison.
                ++skipped_;
                phase_ = Phase::Idle;
                cpu_.set_native(true);
                if (!end_) cpu_.jump(return_address_);
                return;
            }
            std::memcpy(original_regs_, regs, sizeof regs);
            copy_out(after_);
            copy_in(before_);
            cpu_.restore_context(entry_);
            start_run(true);
            return;
        }
        if (phase_ != Phase::Native) return;
        std::string diff;
        if (host_calls)
            diff += "la version native appelle " + std::to_string(host_calls) + " fois une fonction de l'hôte\n";
        for (int i = 0; i < 8; ++i)
            if (!(ignored_ & 1u << i) && regs[i] != original_regs_[i]) {
                char line[96];
                std::snprintf(line, sizeof line, "registre %s : %#010x à l'origine, %#010x en natif\n", kRegNames[i],
                              original_regs_[i], regs[i]);
                diff += line;
            }
        diff += compare_memory(original_regs_[4]);
        if (!diff.empty()) {
            fail(diff);
            return;
        }
        ++compared_;
        phase_ = Phase::Idle;
        if (!end_) cpu_.jump(return_address_);  // ESP est déjà celui d'après le retour
    }

    void fail(const std::string& diff) {
        char head[160];
        std::snprintf(head, sizeof head,
                      "passage %llu, appelé depuis %#010x, arguments %#x %#x %#x %#x %#x %#x :\n",
                      static_cast<unsigned long long>(calls_), caller_, args_[0], args_[1], args_[2], args_[3],
                      args_[4], args_[5]);
        failure_ = head + diff;
        phase_ = Phase::Failed;
        cpu_.set_native(true);
        cpu_.request_stop();
    }

    // Toute la mémoire inscriptible du programme : image, partie distribuée du tas, pile, TEB.
    void collect_ranges() {
        ranges_.clear();
        size_t offset = 0;
        const uint32_t heap_end = process_.heap_end();
        for (auto [start, end] : cpu_.data_ranges()) {
            if (start <= heap_end && heap_end <= end && heap_end > start) end = heap_end;  // tas
            else if (start <= heap_end && heap_end <= end) continue;                       // tas vide
            ranges_.push_back({start, end, offset});
            offset += end - start;
        }
        before_.resize(offset);
        after_.resize(offset);
    }

    void copy_out(std::vector<uint8_t>& copy) {
        for (const Range& r : ranges_) std::memcpy(copy.data() + r.offset, cpu_.host(r.start), r.end - r.start);
    }
    void copy_in(const std::vector<uint8_t>& copy) {
        for (const Range& r : ranges_) std::memcpy(cpu_.host(r.start), copy.data() + r.offset, r.end - r.start);
    }

    // Mémoire après la version native comparée à celle d'après l'original. La pile sous ESP
    // (variables locales des fonctions terminées) n'est pas comparée.
    std::string compare_memory(uint32_t esp) {
        uint64_t differing = 0;
        uint32_t first = 0;
        bool found = false;
        for (const Range& r : ranges_) {
            const uint32_t from = r.start <= entry_esp_ && entry_esp_ < r.end ? std::max(r.start, esp) : r.start;
            const uint8_t* expected = after_.data() + r.offset + (from - r.start);
            const uint8_t* actual = cpu_.host(from);
            const uint32_t size = r.end - from;
            if (from >= r.end || std::memcmp(expected, actual, size) == 0) continue;
            for (uint32_t i = 0; i < size; ++i)
                if (expected[i] != actual[i] && !in_arguments(from + i)) {
                    if (!found) first = from + i;
                    found = true;
                    ++differing;
                }
        }
        if (!found) return {};
        char line[200];
        std::snprintf(line, sizeof line, "mémoire : %llu octets diffèrent, le premier en %#010x (%s)\n",
                      static_cast<unsigned long long>(differing), first, where(first).c_str());
        std::string text = line;
        // Les 16 octets à partir du premier, d'origine puis en natif.
        for (int version = 0; version < 2; ++version) {
            text += version ? "  natif   :" : "  origine :";
            for (uint32_t a = first; a < first + 16; ++a) {
                const uint8_t* byte = version ? cpu_.host(a) : expected_byte(a);
                if (!byte) break;
                std::snprintf(line, sizeof line, " %02x", *byte);
                text += line;
            }
            text += '\n';
        }
        return text;
    }

    // Emplacements des arguments de l'appel vérifié (voir Known).
    bool in_arguments(uint32_t address) const {
        return address - (entry_esp_ + 4) < args_bytes_;
    }

    const uint8_t* expected_byte(uint32_t address) const {
        for (const Range& r : ranges_)
            if (address >= r.start && address < r.end) return after_.data() + r.offset + (address - r.start);
        return nullptr;
    }

    // Ce qu'il y a à une adresse : zone de l'état de la partie, tampon d'affichage, pile, image, tas.
    std::string where(uint32_t address) {
        for (const StateZone& z : kZones)
            if (address >= z.address && address - z.address < z.size) {
                char line[96];
                std::snprintf(line, sizeof line, "état de la partie, %s, octet %u", zone_name(z).c_str(),
                              address - z.address);
                return line;
            }
        const uint32_t frame = cpu_.read_u32(kFrameBuffer);
        const uint32_t frame_bytes = cpu_.read_u32(kRowBytes) * cpu_.read_u32(kScreenHeight);
        if (frame && address >= frame && address - frame < frame_bytes) {
            const uint32_t row_bytes = cpu_.read_u32(kRowBytes);
            char line[96];
            std::snprintf(line, sizeof line, "tampon d'affichage, x %u, y %u", (address - frame) % row_bytes / 2,
                          (address - frame) / row_bytes);
            return line;
        }
        if (address >= entry_esp_ - 0x10000 && address < entry_esp_ + 0x10000) return "pile";
        for (const auto& s : process_.image().sections)
            if (address >= s.address && address - s.address < s.virtual_size) return "section " + s.name;
        return address >= 0x10000000 && address < process_.heap_end() ? "tas" : "autre";
    }

    Process& process_;
    Cpu& cpu_;
    const uint32_t start_, end_, every_;
    uint32_t ignored_ = 0, args_bytes_ = 0;
    uint32_t return_thunk_ = 0;
    Phase phase_ = Phase::Idle;
    bool restarting_ = false;
    uint64_t calls_ = 0, compared_ = 0, skipped_ = 0, host_calls_ = 0;
    CpuContext entry_;
    uint32_t entry_esp_ = 0, caller_ = 0, return_address_ = 0;
    uint32_t args_[6] = {};
    uint32_t original_regs_[8] = {};
    std::vector<Range> ranges_;
    std::vector<uint8_t> before_, after_;
    std::string failure_;
};

std::shared_ptr<Verifier> install_verifier(Process& process, const std::string& target, uint32_t every) {
    const size_t colon = target.find(':');
    const uint32_t start = static_cast<uint32_t>(std::stoul(target.substr(0, colon), nullptr, 16));
    const uint32_t end = colon == std::string::npos ? 0 : static_cast<uint32_t>(std::stoul(target.substr(colon + 1), nullptr, 16));
    return std::make_shared<Verifier>(process, start, end, every);
}

bool report_verifier(Verifier& verifier) { return verifier.report(); }

}  // namespace zeus::win32
