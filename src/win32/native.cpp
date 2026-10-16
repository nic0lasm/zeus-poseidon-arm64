// Fonctions du jeu réécrites pour l'hôte.
//
// Le profil d'exécution (zeus-run --profile) montre qu'une cinquantaine de fonctions font
// presque tout le temps d'exécution. Celles qui figurent ici sont réécrites à la main et
// exécutées à la place du code d'origine ; le reste du jeu reste émulé. Chaque réécriture doit
// laisser le journal d'images d'une exécution --deterministic strictement identique.
//
// Adresses : win32/game_gog.h. Une fonction n'est remplacée que si ses premiers octets sont ceux
// attendus.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"
#include "win32/sprite_clip.h"

namespace zeus::win32 {
namespace {

using namespace gog;

uint32_t u32(Cpu& cpu, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, cpu.host(address), 4);
    return value;
}

// Les routines de dessin lisent la partie visible de l'image que le jeu a calculée avant de les
// appeler (FUN_00509c20 et FUN_00509cd0) : kTargetX, kRows, kColumns… dans game_gog.h.

int32_t i32(Cpu& cpu, uint32_t address) { return static_cast<int32_t>(u32(cpu, address)); }

uint16_t u16(Cpu& cpu, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, cpu.host(address), 2);
    return value;
}

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
void copy_rectangle_from(Cpu& cpu, uint32_t image) {
    const uint8_t* source = cpu.host(image);
    uint8_t* out = target(cpu);
    const int32_t rows = i32(cpu, kRows);
    const uint32_t row_bytes = u32(cpu, kColumns) * 2;
    const uint32_t source_skip = u32(cpu, kSourceSkip), target_skip = u32(cpu, kTargetSkip);
    for (int32_t row = 0; row < rows; ++row) {
        std::memmove(out, source, row_bytes);
        source += row_bytes + source_skip;
        out += row_bytes + target_skip;
    }
}

uint32_t copy_rectangle(Cpu& cpu) {
    copy_rectangle_from(cpu, cpu.arg(0));
    return 0u;
}

// FUN_00416010(source) : même copie, mais le magenta 0xF81F de la source est transparent.
// Les boucles d'origine s'exécutent au moins une fois, même pour un compte nul ou négatif.
void copy_rectangle_keyed_from(Cpu& cpu, uint32_t source_address) {
    const uint8_t* source = cpu.host(source_address);
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
}

uint32_t copy_rectangle_keyed(Cpu& cpu) {
    copy_rectangle_keyed_from(cpu, cpu.arg(0));
    return 0u;
}

// FUN_0050b410 : efface le tampon d'affichage, sauf quand kNoClear l'interdit.
uint32_t clear_frame(Cpu& cpu) {
    const uint32_t buffer = u32(cpu, kFrameBuffer);
    if (u32(cpu, kNoClear) != 0 || buffer == 0) return 0u;
    const uint32_t pixels = u32(cpu, kScreenWidth) * u32(cpu, kScreenHeight);
    std::memset(cpu.host(buffer), 0, static_cast<size_t>((pixels & 0x7FFFFFFF) >> 1) * 4 + ((pixels * 2) & 3));
    return 0u;
}

// Images compressées par suites. Chaque ligne est une suite de segments : l'octet 0xFF suivi
// d'un nombre de pixels transparents, ou un nombre n suivi de n pixels de 16 bits.
//
// FUN_004170b0 : saute les lignes masquées en haut de l'image, en adresses de 32 bits comme
// l'original. Seul l'octet bas d'EAX est relu à chaque segment (MOV AL,[ESI]) et EAX n'est remis à
// zéro qu'en début de ligne ; après n pixels, EAX vaut 2n + 1. Si n >= 128, le compte lu ensuite
// sur la même ligne garde donc 0x100 de trop, et le saut de l'image aussi (0x004170CD à 0x004170E8).
uint32_t skip_rows(Cpu& cpu, uint32_t p) {
    for (int32_t rows = i32(cpu, kSkippedRows); rows > 0; --rows) {
        uint32_t eax = 0;
        for (int32_t left = image_width(cpu); left > 0;) {
            eax = (eax & 0xFFFFFF00u) | *cpu.host(p);
            if ((eax & 0xFF) == 0xFF) {
                eax = (eax & 0xFFFFFF00u) | *cpu.host(p + 1);
                left -= static_cast<int32_t>(eax);
                p += 2;
            } else {
                left -= static_cast<int32_t>(eax);
                eax = eax * 2 + 1;
                p += eax;
            }
        }
    }
    return p;
}

// Pixel d'une image par suites : avec `shadow`, 0x7C00 assombrit de moitié le pixel déjà présent.
void put_run_pixel(uint8_t* out, const uint8_t* source, bool shadow) {
    uint16_t value = pixel(source);
    if (shadow && value == 0x7C00) value = (pixel(out) >> 1) & 0x3DEF;
    put_pixel(out, value);
}

// FUN_004161b0(image, ombre) : dessine une image compressée, sans rognage horizontal.
// Avec `ombre`, un pixel 0x7C00 de l'image assombrit de moitié le pixel déjà présent.
void draw_runs_from(Cpu& cpu, uint32_t image, bool shadow) {
    const uint8_t* p = cpu.host(skip_rows(cpu, image));
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
}

// FUN_004163b0(image, ombre) : même dessin, l'image étant rognée à gauche. Chaque ligne est lue
// depuis son début ; les kSourceSkip / 2 premiers pixels ne sont pas écrits. La ligne s'arrête à la
// largeur de l'image, y compris au milieu d'un segment ; un segment transparent peut la dépasser.
void draw_runs_left_from(Cpu& cpu, uint32_t image, bool shadow) {
    const uint8_t* p = cpu.host(skip_rows(cpu, image));
    if (shadow) require_555(cpu);
    const uint32_t row_bytes = u32(cpu, kRowBytes);
    uint32_t row = u32(cpu, kFrameBuffer) + row_bytes * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2;
    for (int32_t rows = i32(cpu, kRows); rows > 0; --rows, row += row_bytes) {
        const int32_t skip = i32(cpu, kSourceSkip);
        uint8_t* out = cpu.host(row - static_cast<uint32_t>(skip));
        int32_t hidden = skip >> 1, left = image_width(cpu);
        while (left > 0) {
            uint8_t count = *p;
            if (count == 0xFF) {
                const int32_t n = p[1];
                p += 2;
                hidden -= n;
                left -= n;
                out += n * 2;
                continue;
            }
            ++p;
            for (; count != 0; --count, out += 2, p += 2, --left, --hidden) {
                if (left == 0) break;  // l'original passe alors à la ligne suivante (0x00416453)
                if (hidden <= 0) put_run_pixel(out, p, shadow);
            }
            if (left == 0) break;
        }
    }
}

// FUN_004165d0(image, ombre) : rognée à droite. Seuls les kColumns premiers pixels de chaque ligne
// sont écrits, mais chaque segment est lu jusqu'au bout : la ligne finit au premier segment qui
// atteint la largeur de l'image. Un segment de 0 pixel ne fait qu'avancer d'un octet.
void draw_runs_right_from(Cpu& cpu, uint32_t image, bool shadow) {
    const uint8_t* p = cpu.host(skip_rows(cpu, image));
    if (shadow) require_555(cpu);
    const uint32_t row_bytes = u32(cpu, kRowBytes);
    uint32_t row = u32(cpu, kFrameBuffer) + row_bytes * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2;
    for (int32_t r = 0; r < i32(cpu, kRows); ++r, row += row_bytes) {
        uint8_t* out = cpu.host(row);
        int32_t visible = i32(cpu, kColumns), done = 0;
        const int32_t width = image_width(cpu);
        if (width <= 0) continue;
        do {
            const uint8_t count = *p++;
            if (count == 0xFF) {
                const int32_t n = *p++;
                done += n;
                visible -= n;
                out += n * 2;
                continue;
            }
            done += count;
            for (uint8_t i = count; i != 0; --i, p += 2, out += 2, --visible)
                if (visible > 0) put_run_pixel(out, p, shadow);
        } while (done < width);
    }
}

// Images par suites retournées : chaque ligne est écrite de droite à gauche, à partir du bord
// droit de l'image. Les écritures se font en adresses de 32 bits, comme l'original.
void put_run_pixel_at(Cpu& cpu, uint32_t out, const uint8_t* source, bool shadow) {
    put_run_pixel(cpu.host(out), source, shadow);
}

// FUN_00415490(image, ombre) : retournée, non rognée en largeur. La ligne suivante part de la
// position atteinte : pointeur final + kTargetSkip + 2 x largeur + 2 (0x00415661), ce qui ne
// revient au début de la ligne que si la ligne a consommé exactement la largeur de l'image.
void draw_runs_mirrored_from(Cpu& cpu, uint32_t image, bool shadow) {
    const uint8_t* p = cpu.host(skip_rows(cpu, image));
    if (shadow) require_555(cpu);
    const int32_t row_width = image_width(cpu) * 2;
    uint32_t out = u32(cpu, kFrameBuffer) + u32(cpu, kRowBytes) * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2;
    for (int32_t rows = i32(cpu, kRows); rows > 0; --rows) {
        out += static_cast<uint32_t>(row_width - 2);
        for (int32_t left = image_width(cpu); left > 0;) {
            uint8_t count = *p++;
            if (count == 0xFF) {
                const int32_t n = *p++;
                out -= static_cast<uint32_t>(n * 2);
                left -= n;
                continue;
            }
            for (; count != 0; --count, out -= 2, p += 2, --left) {
                if (left == 0) break;  // fin de la ligne (0x00415646)
                put_run_pixel_at(cpu, out, p, shadow);
            }
            if (left == 0) break;
        }
        out += u32(cpu, kTargetSkip) + static_cast<uint32_t>(row_width) + 2;
    }
}

// FUN_004158e0(image, ombre) : retournée, rognée à gauche. Les kSourceSkip / 2 dernières colonnes
// de l'image (les plus à gauche une fois retournées) ne sont pas écrites ; chaque segment est lu
// jusqu'au bout. Les lignes se suivent de kRowBytes en kRowBytes.
void draw_runs_mirrored_left_from(Cpu& cpu, uint32_t image, bool shadow) {
    const uint8_t* p = cpu.host(skip_rows(cpu, image));
    if (shadow) require_555(cpu);
    const int32_t row_width = image_width(cpu) * 2;
    uint32_t base = u32(cpu, kFrameBuffer) + u32(cpu, kRowBytes) * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2;
    for (int32_t rows = i32(cpu, kRows); rows > 0; --rows, base += u32(cpu, kRowBytes)) {
        int32_t left = image_width(cpu);
        const int32_t skip = i32(cpu, kSourceSkip);
        int32_t visible = left - (skip >> 1);
        uint32_t out = base + static_cast<uint32_t>(row_width - skip) - 2;
        while (left > 0) {
            const uint8_t count = *p;
            if (count == 0xFF) {
                const int32_t n = p[1];
                p += 2;
                visible -= n;
                left -= n;
                out -= static_cast<uint32_t>(n * 2);
                continue;
            }
            ++p;
            left -= count;
            for (uint8_t i = count; i != 0; --i, p += 2, out -= 2, --visible)
                if (visible > 0) put_run_pixel_at(cpu, out, p, shadow);
        }
    }
}

// FUN_00415680(image, ombre) : retournée, rognée à droite. Les largeur - kColumns premières
// colonnes de l'image (à droite une fois retournées) ne sont pas écrites ; chaque segment est lu
// jusqu'au bout. Ligne suivante : pointeur final + kRowBytes + 2 (0x004158C7).
void draw_runs_mirrored_right_from(Cpu& cpu, uint32_t image, bool shadow) {
    const uint8_t* p = cpu.host(skip_rows(cpu, image));
    if (shadow) require_555(cpu);
    const int32_t row_width = image_width(cpu) * 2;
    uint32_t out = u32(cpu, kFrameBuffer) + u32(cpu, kRowBytes) * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2;
    for (int32_t rows = i32(cpu, kRows); rows > 0; --rows) {
        int32_t left = image_width(cpu);
        int32_t hidden = left - i32(cpu, kColumns);
        out += static_cast<uint32_t>(row_width - 2);
        while (left > 0) {
            const uint8_t count = *p++;
            if (count == 0xFF) {
                const int32_t n = *p++;
                left -= n;
                hidden -= n;
                out -= static_cast<uint32_t>(n * 2);
                continue;
            }
            left -= count;
            for (uint8_t i = count; i != 0; --i, p += 2, out -= 2, --hidden)
                if (hidden <= 0) put_run_pixel_at(cpu, out, p, shadow);
        }
        out += u32(cpu, kRowBytes) + 2;
    }
}

uint32_t draw_runs_mirrored(Cpu& cpu) {
    draw_runs_mirrored_from(cpu, cpu.arg(0), (cpu.arg(1) & 0xFF) != 0);
    return 0u;
}

uint32_t draw_runs_mirrored_left(Cpu& cpu) {
    draw_runs_mirrored_left_from(cpu, cpu.arg(0), (cpu.arg(1) & 0xFF) != 0);
    return 0u;
}

uint32_t draw_runs_mirrored_right(Cpu& cpu) {
    draw_runs_mirrored_right_from(cpu, cpu.arg(0), (cpu.arg(1) & 0xFF) != 0);
    return 0u;
}

// FUN_00415f50(source) : copie à clé (0xF81F transparent) de lignes entières, sans pas de source.
// Chaque ligne copie max(largeur, 1) pixels si la largeur n'est pas multiple de 4, sinon
// max(largeur, 4) (quatre pixels déroulés, puis un par un) ; au moins une ligne.
void copy_rows_keyed_from(Cpu& cpu, uint32_t source) {
    const uint8_t* in = cpu.host(source);
    uint32_t out = u32(cpu, kFrameBuffer) + u32(cpu, kTargetX) * 2 + u32(cpu, kTargetY) * u32(cpu, kRowBytes);
    const int32_t rows = std::max(i32(cpu, kRows), 1);
    for (int32_t row = 0; row < rows; ++row) {
        const int32_t width = image_width(cpu);
        const int32_t count = (width & 3) ? std::max(width, 1) : std::max(width, 4);
        for (int32_t x = 0; x < count; ++x, in += 2, out += 2)
            if (pixel(in) != 0xF81F) put_pixel(cpu.host(out), pixel(in));
        out += u32(cpu, kTargetSkip);
    }
}

uint32_t copy_rows_keyed(Cpu& cpu) {
    copy_rows_keyed_from(cpu, cpu.arg(0));
    return 0u;
}

uint32_t draw_runs_left(Cpu& cpu) {
    draw_runs_left_from(cpu, cpu.arg(0), (cpu.arg(1) & 0xFF) != 0);
    return 0u;
}

uint32_t draw_runs_right(Cpu& cpu) {
    draw_runs_right_from(cpu, cpu.arg(0), (cpu.arg(1) & 0xFF) != 0);
    return 0u;
}

uint32_t draw_runs(Cpu& cpu) {
    draw_runs_from(cpu, cpu.arg(0), (cpu.arg(1) & 0xFF) != 0);
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

// __strcmpi de la bibliothèque C de Visual C++ 6, sans paramètres régionaux (kLocale nul,
// ce qui est toujours le cas : le jeu n'appelle pas setlocale) : comparaison sans tenir compte
// de la casse des lettres ASCII ; résultat -1, 0 ou 1.
uint32_t strcmpi(Cpu& cpu) {
    if (u32(cpu, kLocale) != 0) throw std::runtime_error("_strcmpi : paramètres régionaux inattendus");
    const uint8_t* a = cpu.host(cpu.arg(0));
    const uint8_t* b = cpu.host(cpu.arg(1));
    auto lower = [](uint8_t c) { return c >= 'A' && c <= 'Z' ? static_cast<uint8_t>(c + 0x20) : c; };
    for (;; ++a, ++b) {
        const uint8_t x = lower(*a), y = lower(*b);
        if (x != y) return x < y ? 0xFFFFFFFFu : 1u;
        if (!*b) return 0u;
    }
}

// Répartiteur de sprites, FUN_005acf50(this, identifiant, x, y, mode, couleur, miroir) : choisit
// l'image désignée par l'identifiant, la rogne au rectangle de dessin (FUN_00509c20 en largeur,
// FUN_00509cd0 en hauteur), puis appelle la routine de dessin qui convient. Il est appelé pour
// chaque sprite de chaque image affichée (65 000 fois par seconde dans une grande ville).
//
// Seuls les cas qui font l'essentiel des appels sont traités ici : image compressée par suites,
// ni rognée en largeur ni retournée, image opaque ni rognée ni retournée, et sol en losange non
// rogné en largeur (native_draw.cpp). Pour tous les autres, la fonction rend la main au code
// d'origine sans avoir rien écrit : le répartiteur d'origine refait alors tout depuis ses
// arguments. Quand le cas est traité, les variables
// écrites sont celles qu'écrit l'original sur ce chemin, avec les mêmes largeurs.
namespace sprite {

void put8(Cpu& cpu, uint32_t address, uint8_t value) { *cpu.host(address) = value; }
void put16(Cpu& cpu, uint32_t address, int16_t value) { std::memcpy(cpu.host(address), &value, 2); }
void put32(Cpu& cpu, uint32_t address, int32_t value) { std::memcpy(cpu.host(address), &value, 4); }
int16_t s16(Cpu& cpu, uint32_t address) { return static_cast<int16_t>(u16(cpu, address)); }

void clip_sprite(Cpu& cpu);

// FUN_0043ff10 (2 x 2 cases), FUN_00440170, FUN_004406b0, FUN_00440fe0, FUN_00441e50 (6 x 6) : le
// socle d'un bâtiment de n x n cases, dessiné losange par losange dans l'ordre où il est rangé
// (formats/sg3.md) : rangée r de min(r, 2n - 2 - r) + 1 cases, centrées, 60 pixels en x et 15 en y
// entre deux voisines. Chaque losange est rogné (FUN_00509c20, FUN_00509cd0) puis dessiné par
// FUN_00435070, FUN_004377b0 ou FUN_00438a90, depuis kSpriteImage + 1 800 x rang + [0x00F29F74].
// Renvoie 58, comme l'original.
uint32_t draw_base(Cpu& cpu, int32_t tiles, int32_t x, int32_t y) {
    put16(cpu, kImageWidth, 0x3A);
    put16(cpu, kImageHeight, 0x1E);
    uint32_t rank = 0;
    for (int32_t r = 0; r <= 2 * tiles - 2; ++r) {
        const int32_t count = std::min(r, 2 * tiles - 2 - r) + 1;
        for (int32_t j = 0; j < count; ++j, ++rank) {
            put32(cpu, kTargetX, x + 60 * j - 30 * (count - 1));
            put32(cpu, kTargetY, y + 15 * r);
            clip_sprite(cpu);
            const uint8_t clip_y = *cpu.host(kClipY);
            if (clip_y == 5) continue;
            const uint32_t image = u32(cpu, kSpriteImage) + rank * 0x708 + u32(cpu, kBaseOffset);
            switch (*cpu.host(kClipX)) {
                case 1: draw_ground_half(cpu, image, clip_y, true); break;
                case 2: draw_ground_half(cpu, image, clip_y, false); break;
                default: draw_ground(cpu, image, clip_y); break;
            }
        }
    }
    return 0x3Au;
}

// `check_only` : s'arrête avant la première écriture, pour savoir si le cas est traité.
std::optional<uint32_t> dispatch(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y, int32_t mode,
                                 uint8_t mirror_arg, bool check_only = false) {
    // Identifiant : banque * 16384 + numéro, en divisions entières signées comme l'original.
    const int32_t bank = id / 16384, number = id % 16384;
    if (number < 1 && bank == 0) return std::nullopt;
    const uint32_t base = self + static_cast<uint32_t>(bank) * 0xCC44;
    const int32_t count = i32(cpu, base + 0xDC34);
    if (count <= number) return std::nullopt;
    const uint32_t table = u32(cpu, base + 0x1000);
    uint32_t entry = table + static_cast<uint32_t>(number) * 0x48;
    if (entry == 0) return std::nullopt;
    uint8_t mirror = 0;
    if (*cpu.host(entry + 0x3E)) {  // image retournée d'une autre entrée
        const int32_t other = i32(cpu, entry + 0x10) + number;
        entry = count < other ? table : table + static_cast<uint32_t>(other) * 0x48;
        mirror = 1;
        if (entry == 0) return std::nullopt;
    }
    const int32_t image = i32(cpu, entry);
    if (image < 1) return std::nullopt;
    const int16_t width16 = s16(cpu, entry + 0x14), height16 = s16(cpu, entry + 0x16);
    const uint8_t kind = *cpu.host(entry + 0x32);
    mirror ^= mirror_arg;
    const bool shadow = *cpu.host(entry + 0x3B) != 0;
    if (*cpu.host(entry + 0x34)) return std::nullopt;  // image chargée à part (fond d'écran…)
    // Écrans où le fond de la carte se dessine d'un bloc (0x005AD65B).
    const uint8_t screen = *cpu.host(kScreen);
    const int32_t submode = i32(cpu, kSubmode);
    const bool special = screen == 0x15 || screen == 0x14 || screen == 0x1A || screen == 0x34 || screen == 0x2D ||
                         (screen == 3 && (submode == 0x14 || submode == 0x15)) ||
                         (screen == 9 && *cpu.host(kSubscreen) == 0x14);
    if (special && static_cast<uint32_t>(id) == u32(cpu, u32(cpu, kBackground) + 4) + 0x3C000) return std::nullopt;
    // Sol en losange de 58 pixels (type 0x1E) : la hauteur de l'image est ramenée à 30 avant le
    // rognage. Les autres largeurs de ce type passent par d'autres routines.
    int16_t height = height16;
    if (kind == 0x1E) {
        if (!ground_known()) return std::nullopt;
        // Socles de 2 à 6 cases (0x005AD5F0 et suivantes) : appelés avant tout rognage.
        int32_t tiles = 0;
        switch (width16) {
            case 0x76: tiles = 2; break;
            case 0xB2: tiles = 3; break;
            case 0xEE: tiles = 4; break;
            case 0x12A: tiles = 5; break;
            case 0x166: tiles = 6; break;
            default: break;
        }
        if (tiles != 0) {
            if (!ground_halves_known() || !bases_known() || u16(cpu, kOverlay) != 0) return std::nullopt;
            if (check_only) return 0u;
            put32(cpu, kSpriteX, x);
            put32(cpu, kSpriteY, y);
            put32(cpu, kSpriteId, id);
            put32(cpu, kSpriteImage, image);
            put16(cpu, kImageWidth, width16);
            put16(cpu, kImageHeight, height16);
            put8(cpu, kSpriteKind, kind);
            put32(cpu, kTargetX, x);
            put32(cpu, kTargetY, y);
            return draw_base(cpu, tiles, x, y);
        }
        if (width16 != 0x3A) return std::nullopt;
        height = 0x1E;
    }

    // Rognage au rectangle de dessin (FUN_00509c20 en largeur, FUN_00509cd0 en hauteur).
    const HorizontalClip h = clip_horizontal(x, width16, i32(cpu, kClipLeft), i32(cpu, kClipRight));
    const int32_t left = h.left, skip_left = h.skip_left, right = h.right, visible = h.visible;
    const uint8_t clip_x = h.state;
    const int32_t source_skip = (width16 - visible) * 2, target_skip = (i32(cpu, kRowPixels) - visible) * 2;
    const VerticalClip v = clip_vertical(y, height, i32(cpu, kClipTop), i32(cpu, kClipBottom));
    const int32_t top = v.top, rows = v.rows, skipped = v.skipped, drawn = v.drawn;
    uint8_t clip_y = v.state;
    if (clip_x == 5) clip_y = 5;

    // Choix de la routine (0x005AD748 et suivantes). Images non compressées : la source avance de
    // (largeur x lignes masquées + colonnes masquées) x 2 octets, écrite dans kSpriteImage.
    enum class Draw { None, Runs, Opaque, Ground, Keyed, KeyedRows } draw = Draw::None;
    uint32_t source = static_cast<uint32_t>(image);
    if (clip_y != 5) {
        if (u16(cpu, kOverlay) != 0) return std::nullopt;
        bool uncompressed = false;
        if (kind == 0x1E) {
            if (clip_x != 0 && !ground_halves_known()) return std::nullopt;
            draw = Draw::Ground;  // FUN_00435070, FUN_004377b0, FUN_00438a90 ; sans retournement
        } else if (kind == 0) {
            if (mode == 1) return std::nullopt;  // teinte, FUN_005b86a0 et variantes
            if (*cpu.host(entry + 0x33) != 0) {
                if (u32(cpu, entry + 0x40) != 0) return std::nullopt;  // transparence, FUN_00413e30…
                draw = Draw::Runs;
            } else {
                if (mirror != 0) return std::nullopt;  // FUN_00415e30, FUN_00415ec0
                uncompressed = true;
                draw = clip_x == 0 ? Draw::KeyedRows : Draw::Keyed;  // FUN_00415f50, FUN_00416010
            }
        } else {
            if (mirror != 0 || kind == 0x0C || kind == 0x14) return std::nullopt;
            if ((kind == 0x0D || kind == 0x0A || kind == 0x02) && clip_x != 0) return std::nullopt;  // carrés fixes
            uncompressed = true;
            draw = clip_x != 0 ? Draw::Keyed : clip_y == 0 ? Draw::Opaque : Draw::KeyedRows;
        }
        if (uncompressed) source += static_cast<uint32_t>((width16 * skipped + skip_left) * 2);
    }

    if (check_only) return 0u;
    put32(cpu, kSpriteX, x);
    put32(cpu, kSpriteY, y);
    put32(cpu, kSpriteId, id);
    put32(cpu, kSpriteImage, static_cast<int32_t>(source));
    put16(cpu, kImageWidth, width16);
    put16(cpu, kImageHeight, height);
    put8(cpu, kSpriteKind, kind);
    put32(cpu, kTargetX, left);
    put32(cpu, kTargetY, top);
    put8(cpu, kClipX, clip_x);
    put32(cpu, kSkipLeft, skip_left);
    put32(cpu, kRight, right);
    put32(cpu, kColumns, visible);
    put32(cpu, kSourceSkip, source_skip);
    put32(cpu, kTargetSkip, target_skip);
    put32(cpu, kVisibleRows, rows);
    put8(cpu, kClipY, clip_y);
    put32(cpu, kSkippedRows, skipped);
    put32(cpu, kRows, drawn);
    switch (draw) {
        case Draw::None: return 0u;  // invisible : l'original renvoie alors 0
        case Draw::Runs:  // FUN_004161b0, FUN_004163b0, FUN_004165d0 ; retournée : FUN_00415490 et variantes
            if (mirror != 0) {
                if (clip_x == 1) draw_runs_mirrored_left_from(cpu, source, shadow);
                else if (clip_x == 2) draw_runs_mirrored_right_from(cpu, source, shadow);
                else draw_runs_mirrored_from(cpu, source, shadow);
            } else if (clip_x == 1) {
                draw_runs_left_from(cpu, source, shadow);
            } else if (clip_x == 2) {
                draw_runs_right_from(cpu, source, shadow);
            } else {
                draw_runs_from(cpu, source, shadow);
            }
            break;
        case Draw::Opaque: copy_rectangle_from(cpu, source); break;
        case Draw::Keyed: copy_rectangle_keyed_from(cpu, source); break;
        case Draw::KeyedRows: copy_rows_keyed_from(cpu, source); break;
        case Draw::Ground:
            if (clip_x == 0) draw_ground(cpu, source, clip_y);
            else draw_ground_half(cpu, source, clip_y, clip_x == 1);
            break;
    }
    return static_cast<uint32_t>(visible);
}

// FUN_00509c20 puis FUN_00509cd0 : rognage de l'image décrite par kTargetX, kTargetY, kImageWidth
// et kImageHeight, avec toutes les variables qu'écrivent les deux fonctions.
void clip_sprite(Cpu& cpu) {
    const int16_t width = s16(cpu, kImageWidth), height = s16(cpu, kImageHeight);
    const HorizontalClip h = clip_horizontal(i32(cpu, kTargetX), width, i32(cpu, kClipLeft), i32(cpu, kClipRight));
    put8(cpu, kClipX, h.state);
    put32(cpu, kSkipLeft, h.skip_left);
    put32(cpu, kRight, h.right);
    put32(cpu, kTargetX, h.left);
    put32(cpu, kColumns, h.visible);
    put32(cpu, kSourceSkip, (width - h.visible) * 2);
    put32(cpu, kTargetSkip, (i32(cpu, kRowPixels) - h.visible) * 2);
    const VerticalClip v = clip_vertical(i32(cpu, kTargetY), height, i32(cpu, kClipTop), i32(cpu, kClipBottom));
    put32(cpu, kVisibleRows, v.rows);
    put8(cpu, kClipY, h.state == 5 ? 5 : v.state);
    put32(cpu, kSkippedRows, v.skipped);
    put32(cpu, kTargetY, v.top);
    put32(cpu, kRows, v.drawn);
}

// FUN_005adf00(this, identifiant, x, y) : la partie haute d'un bâtiment, c'est-à-dire la partie
// compressée qui suit la base en losange dans l'image (entrée +0x35 non nulle). Elle est placée
// d'après la largeur de la base, de 1 à 6 cases ; pour une autre largeur, la position et la
// hauteur restent celles du sprite précédent, comme dans l'original. Le miroir de l'entrée
// (+0x3E) n'est pas suivi. Renvoie 1 quand l'entrée a une partie haute, 0 sinon. Seule la
// surimpression (FUN_00416820 et ses variantes) est laissée à l'original, décidée avant toute
// écriture.
std::optional<uint32_t> building_top(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y) {
    const int32_t bank = id / 16384, number = id % 16384;
    if (number < 1 && bank == 0) return 0u;
    const uint32_t base = self + static_cast<uint32_t>(bank) * 0xCC44;
    if (i32(cpu, base + 0xDC34) <= number) return 0u;
    const uint32_t entry = u32(cpu, base + 0x1000) + static_cast<uint32_t>(number) * 0x48;
    if (*cpu.host(entry + 0x35) == 0) return 0u;
    if (u16(cpu, kOverlay) != 0) return std::nullopt;
    int32_t image = i32(cpu, entry);
    put32(cpu, kSpriteImage, image);
    if (image < 1) return 0u;
    image += i32(cpu, entry + 8);
    put32(cpu, kSpriteImage, image);
    const int16_t width = s16(cpu, entry + 0x14), height = s16(cpu, entry + 0x16);
    put16(cpu, kImageWidth, width);
    // Largeur de la base : décalage en x, en y (ajouté à y - hauteur) et lignes ôtées à la hauteur.
    struct Base {
        int16_t width;
        int32_t dx, dy;
        int16_t lines;
    };
    static constexpr Base kBases[] = {{0x166, -150, 180, 91}, {0x12A, -120, 150, 76}, {0xEE, -90, 120, 61},
                                      {0xB2, -60, 90, 46},    {0x76, -30, 60, 31},    {0x3A, 0, 30, 16}};
    for (const Base& b : kBases) {
        if (width != b.width) continue;
        put32(cpu, kTargetX, x + b.dx);
        put32(cpu, kTargetY, y - height + b.dy);
        put16(cpu, kImageHeight, static_cast<int16_t>(height - b.lines));
        break;
    }
    clip_sprite(cpu);
    if (*cpu.host(kClipY) == 5) return 1u;
    const bool shadow = *cpu.host(entry + 0x3B) != 0;
    switch (*cpu.host(kClipX)) {
        case 1: draw_runs_left_from(cpu, static_cast<uint32_t>(image), shadow); break;
        case 2: draw_runs_right_from(cpu, static_cast<uint32_t>(image), shadow); break;
        default: draw_runs_from(cpu, static_cast<uint32_t>(image), shadow); break;
    }
    return 1u;
}

std::optional<uint32_t> building_top_call(Cpu& cpu) {
    return building_top(cpu, cpu.reg(Cpu::Reg::ECX), static_cast<int32_t>(cpu.arg(0)), static_cast<int32_t>(cpu.arg(1)),
                        static_cast<int32_t>(cpu.arg(2)));
}

std::optional<uint32_t> dispatch_call(Cpu& cpu) {
    return dispatch(cpu, cpu.reg(Cpu::Reg::ECX), static_cast<int32_t>(cpu.arg(0)), static_cast<int32_t>(cpu.arg(1)),
                    static_cast<int32_t>(cpu.arg(2)), static_cast<int32_t>(cpu.arg(3)), static_cast<uint8_t>(cpu.arg(5)));
}

}  // namespace sprite

// Au démarrage, FUN_00505770 lit « Model\Zeus eventmsg.txt » et, pour chaque clé du fichier,
// cherche son nom dans une table de plusieurs milliers d'entrées en appelant _strcmpi sur chacune :
// 18 millions d'appels, l'essentiel du temps de démarrage. La boucle (0x005059F8 à 0x00505A33) se
// termine avec l'indice trouvé, ou le nombre d'entrées, dans EDX et dans [ESP+0x10] ; EAX, ECX et
// les indicateurs sont réécrits avant d'être relus. L'interception fait la même recherche avant
// la première instruction de la boucle, remplacée par un saut à sa sortie.
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
    if (u32(cpu, kLocale) != 0) throw std::runtime_error("recherche de clé : paramètres régionaux inattendus");
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
    {kCopyRectangle, {0xA1, 0x98, 0xA6, 0xF2, 0x00}, 0, copy_rectangle},
    {kCopyRectangleKeyed, {0x55, 0x8B, 0xEC, 0x53, 0x56}, 0, copy_rectangle_keyed},
    {kClearFrame, {0xA1, 0x40, 0xC0, 0x63, 0x00}, 0, clear_frame},
    {kDrawRuns, {0x8B, 0x44, 0x24, 0x04, 0x83}, 0, draw_runs},
    {kDrawRunsLeft, {0x8B, 0x44, 0x24, 0x04, 0x53}, 0, draw_runs_left},
    {kDrawRunsRight, {0x8B, 0x44, 0x24, 0x04, 0x53}, 0, draw_runs_right},
    {kDrawRunsMirrored, {0x51, 0x8B, 0x44, 0x24, 0x08}, 0, draw_runs_mirrored},
    {kDrawRunsMirroredLeft, {0x51, 0x8B, 0x44, 0x24, 0x08}, 0, draw_runs_mirrored_left},
    {kDrawRunsMirroredRight, {0x8B, 0x44, 0x24, 0x04, 0x53}, 0, draw_runs_mirrored_right},
    {kCopyRowsKeyed, {0x55, 0x8B, 0xEC, 0x56, 0x57}, 0, copy_rows_keyed},
    {kDrawRunsBlended, {0xA1, 0x24, 0xAB, 0xF2, 0x00}, 0, draw_runs_blended},
    {kStrcmpi, {0x55, 0x8B, 0xEC, 0x57, 0x56}, 0, strcmpi},
};

}  // namespace

std::optional<uint32_t> draw_sprite(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y, int32_t mode,
                                    uint8_t mirror) {
    return sprite::dispatch(cpu, self, id, x, y, mode, mirror);
}

bool sprite_handled(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y, int32_t mode, uint8_t mirror) {
    return sprite::dispatch(cpu, self, id, x, y, mode, mirror, true).has_value();
}

std::optional<uint32_t> draw_building_top(Cpu& cpu, uint32_t self, int32_t id, int32_t x, int32_t y) {
    return sprite::building_top(cpu, self, id, x, y);
}

void install_native_functions(Process& p) {
    Cpu& cpu = p.cpu();
    int installed = 0;
    for (const Replacement& r : kReplacements) {
        if (!matches(cpu, r.address, r.first_bytes, sizeof r.first_bytes)) continue;
        cpu.replace_function(r.address, r.arg_bytes, r.fn);
        ++installed;
    }
    if (matches(cpu, kSpriteDispatch, {0x81, 0xEC, 0x14, 0x01, 0x00, 0x00, 0x8B, 0x84, 0x24, 0x1C, 0x01, 0x00, 0x00})) {
        cpu.add_fast_path(kSpriteDispatch, 0x18, 6, sprite::dispatch_call);  // « SUB ESP,0x114 » déplacé
        ++installed;
    }
    if (matches(cpu, kBuildingTop, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0xC6, 0x99})) {
        cpu.add_fast_path(kBuildingTop, 0x0C, 5, sprite::building_top_call);  // « PUSH ESI ; MOV ESI,[ESP+8] »
        ++installed;
    }
    if (matches(cpu, kKeySearch, kKeySearchBytes, sizeof kKeySearchBytes)) {
        const uint8_t jump[2] = {0xEB, static_cast<uint8_t>(kKeySearchEnd - (kKeySearch + 2))};
        cpu.patch_code(kKeySearch, jump, sizeof jump);
        cpu.add_native_hook(kKeySearch, search_key);
        ++installed;
    }
    installed += install_native_drawing(p);
    installed += install_native_walkers(p);
    installed += install_native_buildings(p);
    installed += install_native_routes(p);
    installed += install_native_figures(p);
    if (p.trace) std::fprintf(stderr, "[hôte] %d fonctions du jeu remplacées par des fonctions natives\n", installed);
}

}  // namespace zeus::win32
