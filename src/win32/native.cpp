// Fonctions du jeu réécrites pour l'hôte.
//
// Le profil d'exécution (zeus-run --profile) montre qu'une cinquantaine de fonctions font
// presque tout le temps d'exécution. Celles qui figurent ici sont réécrites à la main et
// exécutées à la place du code d'origine ; le reste du jeu reste émulé. Chaque réécriture doit
// laisser le journal d'images d'une exécution --deterministic strictement identique.
//
// Adresses de l'exécutable GOG (SHA-256 382bcd28…). Une fonction n'est remplacée que si ses
// premiers octets sont ceux attendus.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "win32/process.h"

namespace zeus::win32 {
namespace {

uint32_t u32(Cpu& cpu, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, cpu.host(address), 4);
    return value;
}

// Variables du jeu utilisées par ses routines de dessin. Avant d'appeler l'une d'elles, le jeu a
// calculé la partie visible de l'image (FUN_00509c20 et FUN_00509cd0).
constexpr uint32_t kFrameBuffer = 0x00F9C100,  // adresse du tampon d'affichage
                   kRowBytes = 0x00F2A698,     // octets par ligne du tampon
                   kTargetY = 0x00F13A3C, kTargetX = 0x00F13A40,
                   kRows = 0x00F13A34,         // lignes à dessiner
                   kColumns = 0x00F139A4,      // pixels visibles par ligne
                   kSourceSkip = 0x00F139A0,   // octets de source à sauter après chaque ligne
                   kTargetSkip = 0x00F1399C,   // octets de destination à sauter après chaque ligne
                   kImageWidth = 0x00F13A1E,   // largeur de l'image, sur 16 bits
                   kSkippedRows = 0x00F13998,  // lignes de l'image masquées en haut
                   kPixelFormat = 0x00F2AB24,  // 0x565 ou 0x555
                   kScreenWidth = 0x00F2A6B4, kScreenHeight = 0x00F2A6B0;

int32_t i32(Cpu& cpu, uint32_t address) { return static_cast<int32_t>(u32(cpu, address)); }

int32_t image_width(Cpu& cpu) {
    int16_t value;
    std::memcpy(&value, cpu.host(kImageWidth), 2);
    return value;
}

uint16_t pixel(const uint8_t* p) {
    uint16_t value;
    std::memcpy(&value, p, 2);
    return value;
}

void put_pixel(uint8_t* p, uint16_t value) { std::memcpy(p, &value, 2); }

// Premier pixel à écrire dans le tampon d'affichage.
uint8_t* target(Cpu& cpu) {
    return cpu.host(u32(cpu, kFrameBuffer) + u32(cpu, kRowBytes) * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2);
}

// L'hôte annonce toujours des surfaces 5-5-5 : les variantes 5-6-5 de ces routines ne sont pas
// réécrites, faute de pouvoir les vérifier.
void require_555(Cpu& cpu) {
    if (u32(cpu, kPixelFormat) == 0x565) throw std::runtime_error("dessin natif : format 5-6-5 inattendu");
}

// FUN_00413dc0(source) : copie un rectangle d'image opaque vers le tampon d'affichage.
uint32_t copy_rectangle(Cpu& cpu) {
    const uint8_t* source = cpu.host(cpu.arg(0));
    uint8_t* out = target(cpu);
    const int32_t rows = i32(cpu, kRows);
    const uint32_t row_bytes = u32(cpu, kColumns) * 2;
    const uint32_t source_skip = u32(cpu, kSourceSkip), target_skip = u32(cpu, kTargetSkip);
    for (int32_t row = 0; row < rows; ++row) {
        std::memmove(out, source, row_bytes);
        source += row_bytes + source_skip;
        out += row_bytes + target_skip;
    }
    return 0u;
}

// FUN_00416010(source) : même copie, mais le magenta 0xF81F de la source est transparent.
// Les boucles d'origine s'exécutent au moins une fois, même pour un compte nul ou négatif.
uint32_t copy_rectangle_keyed(Cpu& cpu) {
    const uint8_t* source = cpu.host(cpu.arg(0));
    uint8_t* out = target(cpu);
    const uint32_t source_skip = u32(cpu, kSourceSkip), target_skip = u32(cpu, kTargetSkip);
    const int32_t rows = std::max(i32(cpu, kRows), 1);
    for (int32_t row = 0; row < rows; ++row) {
        const int32_t columns = std::max(i32(cpu, kColumns), 1);
        for (int32_t x = 0; x < columns; ++x, source += 2, out += 2)
            if (pixel(source) != 0xF81F) put_pixel(out, pixel(source));
        source += source_skip;
        out += target_skip;
    }
    return 0u;
}

// FUN_0050b410 : efface le tampon d'affichage, sauf quand 0x63C040 l'interdit.
uint32_t clear_frame(Cpu& cpu) {
    const uint32_t buffer = u32(cpu, kFrameBuffer);
    if (u32(cpu, 0x0063C040) != 0 || buffer == 0) return 0u;
    const uint32_t pixels = u32(cpu, kScreenWidth) * u32(cpu, kScreenHeight);
    std::memset(cpu.host(buffer), 0, static_cast<size_t>((pixels & 0x7FFFFFFF) >> 1) * 4 + ((pixels * 2) & 3));
    return 0u;
}

// Images compressées par suites. Chaque ligne est une suite de segments : l'octet 0xFF suivi
// d'un nombre de pixels transparents, ou un nombre n suivi de n pixels de 16 bits.
//
// FUN_004170b0 : saute les lignes masquées en haut de l'image.
const uint8_t* skip_rows(Cpu& cpu, const uint8_t* p) {
    for (int32_t rows = i32(cpu, kSkippedRows); rows > 0; --rows) {
        for (int32_t left = image_width(cpu); left > 0;) {
            if (*p == 0xFF) {
                left -= p[1];
                p += 2;
            } else {
                left -= *p;
                p += *p * 2 + 1;
            }
        }
    }
    return p;
}

// FUN_004161b0(image, ombre) : dessine une image compressée, sans rognage horizontal.
// Avec `ombre`, un pixel 0x7C00 de l'image assombrit de moitié le pixel déjà présent.
uint32_t draw_runs(Cpu& cpu) {
    const uint8_t* p = skip_rows(cpu, cpu.host(cpu.arg(0)));
    const bool shadow = (cpu.arg(1) & 0xFF) != 0;
    uint8_t* out = target(cpu);
    const uint32_t target_skip = u32(cpu, kTargetSkip);
    if (shadow) require_555(cpu);
    for (int32_t rows = i32(cpu, kRows); rows > 0; --rows) {
        int32_t left = image_width(cpu);
        if (!shadow) {
            while (left > 0) {
                int32_t count = *p;
                if (count == 0xFF) {
                    count = p[1];
                    p += 2;
                } else {
                    count = std::min(count, left);  // comme l'original, qui avance alors d'autant
                    std::memmove(out, p + 1, static_cast<size_t>(count) * 2);
                    p += 1 + count * 2;
                }
                out += count * 2;
                left -= count;
            }
        } else {
            while (left > 0) {
                uint8_t count = *p;
                if (count == 0xFF) {
                    left -= p[1];
                    out += p[1] * 2;
                    p += 2;
                    continue;
                }
                ++p;
                for (; count != 0; --count) {
                    if (left == 0) break;  // l'original abandonne la ligne ici, sans finir le segment
                    uint16_t value = pixel(p);
                    if (value == 0x7C00) value = (pixel(out) >> 1) & 0x3DEF;
                    put_pixel(out, value);
                    out += 2;
                    p += 2;
                    --left;
                }
            }
        }
        out += target_skip;
    }
    return 0u;
}

// FUN_00413e30(image, transparence) : dessine une image compressée en la mélangeant au fond. La
// transparence est une seconde suite de segments, parallèle à la première : un octet de 0 à 31
// par pixel. Mélange par composante : (a * image + (31 - a) * fond) >> 5.
uint32_t draw_runs_blended(Cpu& cpu) {
    require_555(cpu);
    const uint8_t* p = cpu.host(cpu.arg(0));
    const uint8_t* alpha = cpu.host(cpu.arg(1));
    // FUN_00417040 : saute les lignes masquées en haut, dans les deux suites.
    for (int32_t rows = i32(cpu, kSkippedRows); rows > 0; --rows) {
        for (int32_t left = image_width(cpu); left > 0;) {
            int32_t count = *p;
            if (count == 0xFF) {
                count = p[1];
                p += 2;
                alpha += 2;
            } else {
                p += 1 + count * 2;
                alpha += 1 + count;
            }
            left -= count;
        }
    }
    uint8_t* out = target(cpu);
    const uint32_t target_skip = u32(cpu, kTargetSkip);
    const int32_t rows = i32(cpu, kRows), width = image_width(cpu);
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t x = 0; x < width;) {
            const uint8_t count = *p;
            if (count == 0xFF) {
                x += p[1];
                out += p[1] * 2;
                p += 2;
                alpha += 2;
                continue;
            }
            ++p;
            ++alpha;
            x += count;
            for (uint8_t i = 0; i < count; ++i, p += 2, ++alpha, out += 2) {
                const uint32_t a = *alpha;
                if (a == 0) continue;
                const uint32_t image = pixel(p), back = pixel(out), b = 31 - a;
                const uint32_t blue = (((back & 0x1F) * b + (image & 0x1F) * a) >> 5) & 0x1F;
                const uint32_t red = ((((back >> 10) & 0x1F) * b + ((image >> 10) & 0x1F) * a) & 0x3E0) << 5;
                const uint32_t green = (((back >> 5) & 0x1F) * b + ((image >> 5) & 0x1F) * a) & 0x3E0;
                put_pixel(out, static_cast<uint16_t>(blue | red | green));
            }
        }
        out += target_skip;
    }
    return 0u;
}

// __strcmpi de la bibliothèque C de Visual C++ 6, sans paramètres régionaux (0x160BFA4 nul,
// ce qui est toujours le cas : le jeu n'appelle pas setlocale) : comparaison sans tenir compte
// de la casse des lettres ASCII ; résultat -1, 0 ou 1.
uint32_t strcmpi(Cpu& cpu) {
    if (u32(cpu, 0x0160BFA4) != 0) throw std::runtime_error("_strcmpi : paramètres régionaux inattendus");
    const uint8_t* a = cpu.host(cpu.arg(0));
    const uint8_t* b = cpu.host(cpu.arg(1));
    auto lower = [](uint8_t c) { return c >= 'A' && c <= 'Z' ? static_cast<uint8_t>(c + 0x20) : c; };
    for (;; ++a, ++b) {
        const uint8_t x = lower(*a), y = lower(*b);
        if (x != y) return x < y ? 0xFFFFFFFFu : 1u;
        if (!*b) return 0u;
    }
}

// Au démarrage, FUN_00505770 lit « Model\Zeus eventmsg.txt » et, pour chaque clé du fichier,
// cherche son nom dans une table de plusieurs milliers d'entrées en appelant _strcmpi sur chacune :
// 18 millions d'appels, l'essentiel du temps de démarrage. La boucle (0x005059F8 à 0x00505A33) se
// termine avec l'indice trouvé, ou le nombre d'entrées, dans EDX et dans [ESP+0x10] ; EAX, ECX et
// les indicateurs sont réécrits avant d'être relus. L'interception fait la même recherche avant
// la première instruction de la boucle, remplacée par un saut à sa sortie.
constexpr uint32_t kKeySearch = 0x005059F8, kKeySearchEnd = 0x00505A33;
constexpr uint8_t kKeySearchBytes[] = {0x8B, 0x43, 0x0C, 0x33, 0xD2, 0x85, 0xC0, 0x89, 0x54, 0x24, 0x10,
                                       0x7E, 0x2E, 0xEB, 0x04, 0x8B, 0x54, 0x24, 0x10, 0x8B, 0x0B, 0x57,
                                       0x8B, 0x54, 0xD1, 0x04, 0x52, 0xE8, 0x28, 0x6C, 0x0C, 0x00};

bool same_text(const uint8_t* a, const uint8_t* b) {
    auto lower = [](uint8_t c) { return c >= 'A' && c <= 'Z' ? static_cast<uint8_t>(c + 0x20) : c; };
    for (;; ++a, ++b) {
        if (lower(*a) != lower(*b)) return false;
        if (!*b) return true;
    }
}

void search_key(Cpu& cpu) {
    if (u32(cpu, 0x0160BFA4) != 0) throw std::runtime_error("recherche de clé : paramètres régionaux inattendus");
    const uint32_t table = cpu.reg(Cpu::Reg::EBX);  // EBX : l'objet ; [EBX] la table, [EBX+0xC] sa taille
    const uint8_t* key = cpu.host(cpu.reg(Cpu::Reg::EDI));
    const int32_t count = static_cast<int32_t>(u32(cpu, table + 0x0C));
    const uint32_t entries = u32(cpu, table);
    int32_t found = 0;
    while (found < count && !same_text(cpu.host(u32(cpu, entries + 8 * static_cast<uint32_t>(found) + 4)), key)) ++found;
    cpu.set_reg(Cpu::Reg::EDX, static_cast<uint32_t>(found));
    std::memcpy(cpu.host(cpu.reg(Cpu::Reg::ESP) + 0x10), &found, 4);
}

struct Replacement {
    uint32_t address;
    uint8_t first_bytes[5];
    uint32_t arg_bytes;
    uint32_t (*fn)(Cpu&);
};

constexpr Replacement kReplacements[] = {
    {0x00413DC0, {0xA1, 0x98, 0xA6, 0xF2, 0x00}, 0, copy_rectangle},
    {0x00416010, {0x55, 0x8B, 0xEC, 0x53, 0x56}, 0, copy_rectangle_keyed},
    {0x0050B410, {0xA1, 0x40, 0xC0, 0x63, 0x00}, 0, clear_frame},
    {0x004161B0, {0x8B, 0x44, 0x24, 0x04, 0x83}, 0, draw_runs},
    {0x00413E30, {0xA1, 0x24, 0xAB, 0xF2, 0x00}, 0, draw_runs_blended},
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
    if (std::memcmp(cpu.host(kKeySearch), kKeySearchBytes, sizeof kKeySearchBytes) == 0) {
        const uint8_t jump[2] = {0xEB, static_cast<uint8_t>(kKeySearchEnd - (kKeySearch + 2))};
        std::memcpy(cpu.host(kKeySearch), jump, sizeof jump);
        cpu.add_code_hook(kKeySearch, search_key);
        ++installed;
    }
    if (p.trace) std::fprintf(stderr, "[hôte] %d fonctions du jeu remplacées par des fonctions natives\n", installed);
}

}  // namespace zeus::win32
