// Recouvrement, transparence et masques de glyphes du dessin, réécrits pour l'hôte.
//
// Des routines de pixels rares dans une partie (docs/engine/rendering.md, § 8 et 9), que le
// scénario « rare » du banc atteint :
//   - le recouvrement : quand la ville marque un bâtiment en rouge (couleur kOverlay, 0xF863), le
//     répartiteur de sprites et FUN_005adf00 redessinent les pixels de l'image par suites avec
//     fond &= couleur (FUN_00416820 et ses deux variantes rognées) ; pour une partie haute retournée,
//     FUN_005ae1b0 la redessine retournée avec source & couleur (FUN_00415b80 et ses variantes) ;
//   - la transparence : une image par suites accompagnée d'une seconde suite, un octet de 0 à 31
//     par pixel, mélangée au fond (FUN_00413e30 dans native.cpp, ses cinq variantes rognées et
//     retournées ici) ;
//   - le masque de glyphe (type 20) : chaque pixel d'une image par suites prend une seule couleur,
//     kGlyphColour (FUN_00416c30, sans rognage horizontal ; ses variantes rognées, que nul
//     scénario n'appelle, restent à l'original).
//
// Chaque routine est remplacée (Cpu::replace_function) et sert aussi au répartiteur natif et aux
// parties hautes natives (native.cpp), qui ne s'en servent que si blending_known() ; le chemin
// rapide de FUN_005ae1b0, la partie haute retournée, n'est installé qu'à cette condition. Elles écrivent
// en adresses de 32 bits, comme l'original, et en reprennent les bizarreries, signalées en
// commentaire : ce sont elles que la vérification appel par appel compare.
//
// Adresses : win32/game_gog.h. Rien n'est remplacé si un octet d'une des routines diffère de
// celui attendu (empreinte FNV-1a de chacune).

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

bool g_known = false;

uint32_t u32(Cpu& cpu, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, cpu.host(address), 4);
    return value;
}
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
uint16_t get(Cpu& cpu, uint32_t address) { return pixel(cpu.host(address)); }
void put(Cpu& cpu, uint32_t address, uint16_t value) { std::memcpy(cpu.host(address), &value, 2); }

// Premier pixel à écrire : kFrameBuffer + kRowBytes x kTargetY + 2 x kTargetX.
uint32_t first_pixel(Cpu& cpu) {
    return u32(cpu, kFrameBuffer) + u32(cpu, kRowBytes) * u32(cpu, kTargetY) + u32(cpu, kTargetX) * 2;
}

// --- Recouvrement ------------------------------------------------------------------------------

// FUN_00416820(image, couleur) : fond &= couleur sous chaque pixel de l'image, sans rognage
// horizontal ; FUN_00416c30(image), le masque de glyphe (type 20), écrit à la place la couleur
// kGlyphColour (`fill`). Les lignes masquées en haut sont sautées comme dans FUN_004161b0
// (FUN_004170b0). Le nombre de pixels d'un segment passe par une table de 16 cas : 1 à 14 et 16
// sont traités tels quels, tout autre nombre (0, 15, 17 et plus) comme 15 pixels (le cas par
// défaut, 0x00416A73 et 0x00416E87). La ligne suivante part de là où la précédente s'est arrêtée,
// plus kTargetSkip ; au moins une ligne (la boucle teste le compte à la fin, 0x00416AD9).
void runs_whole(Cpu& cpu, uint32_t image, uint16_t colour, bool fill) {
    const uint8_t* p = cpu.host(skip_run_rows(cpu, image));
    uint32_t out = first_pixel(cpu);
    int32_t rows = i32(cpu, kRows);
    do {
        for (int32_t left = image_width(cpu); left > 0;) {
            const uint8_t count = *p;
            if (count == 0xFF) {
                out += 2u * p[1];
                left -= p[1];
                p += 2;
                continue;
            }
            const int32_t n = count >= 1 && count <= 16 ? count : 15;
            for (int32_t i = 0; i < n; ++i, out += 2) put(cpu, out, fill ? colour : get(cpu, out) & colour);
            p += 1 + 2 * n;
            left -= n;
        }
        out += u32(cpu, kTargetSkip);
    } while (--rows > 0);
}

// FUN_00416af0(image, couleur), rognée à gauche : chaque ligne part de la cible moins kSourceSkip
// octets et ses kSourceSkip / 2 premiers pixels ne sont pas touchés. FUN_00416b90, rognée à
// droite : seuls les kColumns premiers pixels le sont. Dans les deux, un segment est traité
// jusqu'au bout, la ligne s'arrête au premier segment qui atteint la largeur, et un segment de
// 0 pixel en compte un (la boucle teste le compte à la fin, 0x00416B64). Au moins une ligne.
void overlay_runs_clipped(Cpu& cpu, uint32_t image, uint16_t colour, bool left_side) {
    const uint8_t* p = cpu.host(skip_run_rows(cpu, image));
    uint32_t row = first_pixel(cpu);
    int32_t rows = i32(cpu, kRows);
    do {
        int32_t left = image_width(cpu);
        // À gauche : pixels encore masqués ; à droite : pixels encore visibles.
        int32_t counter = left_side ? static_cast<int32_t>(u32(cpu, kSourceSkip) >> 1) : i32(cpu, kColumns);
        uint32_t out = left_side ? row - 2u * static_cast<uint32_t>(counter) : row;
        while (left > 0) {
            int32_t count = *p;
            if (count == 0xFF) {
                const int32_t n = p[1];
                out += 2u * static_cast<uint32_t>(n);
                p += 2;
                counter -= n;
                left -= n;
                continue;
            }
            ++p;
            do {
                if (left_side ? counter <= 0 : counter > 0) put(cpu, out, get(cpu, out) & colour);
                p += 2;
                out += 2;
                --left;
                --counter;
            } while (--count > 0);
        }
        row += u32(cpu, kRowBytes);
    } while (--rows > 0);
}

// FUN_00415b80(image, couleur), FUN_00415d30 (rognée à gauche), FUN_00415c40 (à droite) : l'image
// retournée redessinée avec source & couleur, sans ombre. Mêmes parcours que FUN_00415490,
// FUN_004158e0 et FUN_00415680 (native.cpp), dont elles ne diffèrent que par le pixel écrit.
void coloured_mirrored(Cpu& cpu, uint32_t image, uint16_t colour, uint8_t clip_x) {
    const uint8_t* p = cpu.host(skip_run_rows(cpu, image));
    const int32_t row_width = image_width(cpu) * 2;
    uint32_t out = first_pixel(cpu);
    const uint32_t base_start = out;
    for (int32_t rows = i32(cpu, kRows), r = 0; r < rows; ++r) {
        int32_t left = image_width(cpu);
        if (clip_x == 0) {
            // Ligne suivante : pointeur final + kTargetSkip + 2 x largeur + 2 (0x00415C2C).
            out += static_cast<uint32_t>(row_width - 2);
            while (left > 0) {
                uint8_t count = *p++;
                if (count == 0xFF) {
                    const int32_t n = *p++;
                    out -= 2u * static_cast<uint32_t>(n);
                    left -= n;
                    continue;
                }
                for (; count != 0; --count, out -= 2, p += 2, --left) {
                    if (left == 0) break;  // fin de la ligne (0x00415C06)
                    put(cpu, out, pixel(p) & colour);
                }
                if (left == 0) break;
            }
            out += u32(cpu, kTargetSkip) + static_cast<uint32_t>(row_width) + 2;
        } else if (clip_x == 2) {
            // Les largeur - kColumns premières colonnes ne sont pas écrites ; ligne suivante :
            // pointeur final + kRowBytes + 2 (0x00415D0B).
            int32_t hidden = left - i32(cpu, kColumns);
            out += static_cast<uint32_t>(row_width - 2);
            while (left > 0) {
                const uint8_t count = *p++;
                if (count == 0xFF) {
                    const int32_t n = *p++;
                    left -= n;
                    hidden -= n;
                    out -= 2u * static_cast<uint32_t>(n);
                    continue;
                }
                left -= count;
                for (uint8_t i = count; i != 0; --i, p += 2, out -= 2, --hidden)
                    if (hidden <= 0) put(cpu, out, pixel(p) & colour);
            }
            out += u32(cpu, kRowBytes) + 2;
        } else {
            // Les kSourceSkip / 2 dernières colonnes de l'image ne sont pas écrites ; les lignes
            // se suivent de kRowBytes en kRowBytes.
            const uint32_t base = base_start + static_cast<uint32_t>(r) * u32(cpu, kRowBytes);
            const int32_t skip = i32(cpu, kSourceSkip);
            int32_t visible = left - (skip >> 1);
            uint32_t at = base + static_cast<uint32_t>(row_width - skip) - 2;
            while (left > 0) {
                const uint8_t count = *p;
                if (count == 0xFF) {
                    const int32_t n = p[1];
                    p += 2;
                    visible -= n;
                    left -= n;
                    at -= 2u * static_cast<uint32_t>(n);
                    continue;
                }
                ++p;
                left -= count;
                for (uint8_t i = count; i != 0; --i, p += 2, at -= 2, --visible)
                    if (visible > 0) put(cpu, at, pixel(p) & colour);
            }
        }
    }
}

// --- Transparence ------------------------------------------------------------------------------

// Mélange par composante 5-5-5 : (a x image + (31 - a) x fond) >> 5. L'original multiplie par un
// registre dont seuls les 16 bits bas valent a (0x004140B2 et suivantes) ; les 16 bits hauts, qui
// viennent d'un pointeur ou d'un compteur, ne changent pas les bits gardés par les masques.
uint16_t blend(uint16_t image, uint16_t back, uint32_t a) {
    const uint32_t b = 31 - a;
    const uint32_t blue = (((back & 0x1Fu) * b + (image & 0x1Fu) * a) >> 5) & 0x1F;
    const uint32_t red = ((((back >> 10) & 0x1Fu) * b + ((image >> 10) & 0x1Fu) * a) << 5) & 0x7C00;
    const uint32_t green = (((back >> 5) & 0x1Fu) * b + ((image >> 5) & 0x1Fu) * a) & 0x3E0;
    return static_cast<uint16_t>(blue | red | green);
}

void blend_at(Cpu& cpu, uint32_t out, const uint8_t* p, const uint8_t* alpha) {
    if (*alpha != 0) put(cpu, out, blend(pixel(p), get(cpu, out), *alpha));
}

// FUN_00417040 : saute les lignes masquées en haut, dans les deux suites.
void skip_blended_rows(Cpu& cpu, const uint8_t*& p, const uint8_t*& alpha) {
    for (int32_t rows = i32(cpu, kSkippedRows); rows > 0; --rows) {
        for (int32_t left = image_width(cpu); left > 0;) {
            const int32_t count = *p;
            if (count == 0xFF) {
                left -= p[1];
                p += 2;
                alpha += 2;
            } else {
                left -= count;
                p += 1 + count * 2;
                alpha += 1 + count;
            }
        }
    }
}

// FUN_00414540 (rognée à droite) et FUN_00414ce0 (à gauche), non retournées. Les lignes se
// suivent de kRowBytes en kRowBytes ; une ligne s'arrête au premier segment qui atteint la largeur.
// À droite, seuls les kColumns premiers pixels sont écrits ; à gauche, la ligne part de la cible
// moins kSourceSkip octets et ses kSourceSkip / 2 premiers pixels ne sont pas écrits.
void draw_blended_clipped(Cpu& cpu, uint32_t image, uint32_t table, bool left_side) {
    const uint8_t* p = cpu.host(image);
    const uint8_t* alpha = cpu.host(table);
    skip_blended_rows(cpu, p, alpha);
    uint32_t row = first_pixel(cpu) - (left_side ? u32(cpu, kSourceSkip) : 0u);
    for (int32_t r = 0; r < i32(cpu, kRows); ++r, row += u32(cpu, kRowBytes)) {
        int32_t counter = left_side ? i32(cpu, kSourceSkip) >> 1 : i32(cpu, kColumns);
        uint32_t out = row;
        for (int32_t x = 0; x < image_width(cpu);) {
            const uint8_t count = *p++;
            ++alpha;
            if (count == 0xFF) {
                const int32_t n = *p++;
                ++alpha;
                x += n;
                counter -= n;
                out += 2u * static_cast<uint32_t>(n);
                continue;
            }
            x += count;
            for (uint8_t i = count; i != 0; --i, p += 2, ++alpha, out += 2, --counter)
                if (left_side ? counter <= 0 : counter > 0) blend_at(cpu, out, p, alpha);
        }
    }
}

// FUN_00414190, retournée, non rognée en largeur : le pixel de rang x d'une ligne va en
// début de ligne + 2 x (largeur - x), un pixel à droite de l'image retournée (0x0041428E) : la
// colonne de la largeur est écrite, la colonne 0 jamais.
void draw_blended_mirrored(Cpu& cpu, uint32_t image, uint32_t table) {
    const uint8_t* p = cpu.host(image);
    const uint8_t* alpha = cpu.host(table);
    skip_blended_rows(cpu, p, alpha);
    uint32_t row = first_pixel(cpu);
    for (int32_t r = 0; r < i32(cpu, kRows); ++r, row += u32(cpu, kRowBytes)) {
        const int32_t width = image_width(cpu);
        for (int32_t x = 0; x < width;) {
            const uint8_t count = *p++;
            ++alpha;
            if (count == 0xFF) {
                x += *p++;
                ++alpha;
                continue;
            }
            for (uint8_t i = count; i != 0; --i, p += 2, ++alpha, ++x)
                blend_at(cpu, row + 2u * static_cast<uint32_t>(width - x), p, alpha);
        }
    }
}

// FUN_00414910 (retournée, rognée à droite) et FUN_004150b0 (à gauche) : mêmes parcours que
// FUN_00415680 et FUN_004158e0. À droite, les largeur - kColumns premières colonnes de l'image ne
// sont pas écrites et la ligne suivante part du pointeur final + kRowBytes + 2 (0x00414CC9). À
// gauche, les lignes se suivent de kRowBytes en kRowBytes et les kSourceSkip / 2 dernières
// colonnes de l'image ne sont pas écrites.
void draw_blended_mirrored_clipped(Cpu& cpu, uint32_t image, uint32_t table, bool left_side) {
    const uint8_t* p = cpu.host(image);
    const uint8_t* alpha = cpu.host(table);
    skip_blended_rows(cpu, p, alpha);
    uint32_t out = first_pixel(cpu);
    const uint32_t base_start = out;
    for (int32_t rows = i32(cpu, kRows), r = 0; r < rows; ++r) {
        int32_t left = image_width(cpu);
        const int32_t row_width = left * 2;
        int32_t counter;  // à droite : colonnes encore masquées ; à gauche : colonnes encore visibles
        if (left_side) {
            const int32_t skip = i32(cpu, kSourceSkip);
            counter = left - (skip >> 1);
            out = base_start + static_cast<uint32_t>(r) * u32(cpu, kRowBytes) + static_cast<uint32_t>(row_width - skip) - 2;
        } else {
            counter = left - i32(cpu, kColumns);
            out += static_cast<uint32_t>(row_width - 2);
        }
        while (left > 0) {
            const uint8_t count = *p;
            if (count == 0xFF) {
                const int32_t n = p[1];
                p += 2;
                alpha += 2;
                left -= n;
                counter -= n;
                out -= 2u * static_cast<uint32_t>(n);
                continue;
            }
            ++p;
            ++alpha;
            left -= count;
            for (uint8_t i = count; i != 0; --i, p += 2, ++alpha, out -= 2, --counter)
                if (left_side ? counter > 0 : counter <= 0) blend_at(cpu, out, p, alpha);
        }
        if (!left_side) out += u32(cpu, kRowBytes) + 2;
    }
}

void require_555(Cpu& cpu) {
    if (u32(cpu, kPixelFormat) == 0x565) throw std::runtime_error("transparence native : format 5-6-5 inattendu");
}

uint32_t overlay_whole_call(Cpu& cpu) {
    runs_whole(cpu, cpu.arg(0), static_cast<uint16_t>(cpu.arg(1)), false);
    return 0u;
}
uint32_t glyph_mask_call(Cpu& cpu) {
    runs_whole(cpu, cpu.arg(0), get(cpu, kGlyphColour), true);
    return 0u;
}
uint32_t overlay_left_call(Cpu& cpu) {
    overlay_runs_clipped(cpu, cpu.arg(0), static_cast<uint16_t>(cpu.arg(1)), true);
    return 0u;
}
uint32_t overlay_right_call(Cpu& cpu) {
    overlay_runs_clipped(cpu, cpu.arg(0), static_cast<uint16_t>(cpu.arg(1)), false);
    return 0u;
}
uint32_t coloured_whole_call(Cpu& cpu) {
    coloured_mirrored(cpu, cpu.arg(0), static_cast<uint16_t>(cpu.arg(1)), 0);
    return 0u;
}
uint32_t coloured_left_call(Cpu& cpu) {
    coloured_mirrored(cpu, cpu.arg(0), static_cast<uint16_t>(cpu.arg(1)), 1);
    return 0u;
}
uint32_t coloured_right_call(Cpu& cpu) {
    coloured_mirrored(cpu, cpu.arg(0), static_cast<uint16_t>(cpu.arg(1)), 2);
    return 0u;
}
uint32_t blended_left_call(Cpu& cpu) {
    draw_runs_blended_variant(cpu, cpu.arg(0), cpu.arg(1), false, 1);
    return 0u;
}
uint32_t blended_right_call(Cpu& cpu) {
    draw_runs_blended_variant(cpu, cpu.arg(0), cpu.arg(1), false, 2);
    return 0u;
}
uint32_t blended_mirrored_call(Cpu& cpu) {
    draw_runs_blended_variant(cpu, cpu.arg(0), cpu.arg(1), true, 0);
    return 0u;
}
uint32_t blended_mirrored_left_call(Cpu& cpu) {
    draw_runs_blended_variant(cpu, cpu.arg(0), cpu.arg(1), true, 1);
    return 0u;
}
uint32_t blended_mirrored_right_call(Cpu& cpu) {
    draw_runs_blended_variant(cpu, cpu.arg(0), cpu.arg(1), true, 2);
    return 0u;
}

uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t a = start; a < end; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

struct Routine {
    uint32_t address, end;
    uint64_t hash;
    uint32_t (*fn)(Cpu&);  // nul : vérifiée seulement (une routine que les autres appellent)
};

constexpr Routine kRoutines[] = {
    {kOverlayRuns, 0x00416AE3, 0xDA88402E37184247ull, overlay_whole_call},
    {kOverlayRunsLeft, 0x00416B8B, 0xDCC39724BC92A75Cull, overlay_left_call},
    {kOverlayRunsRight, 0x00416C25, 0x9C5F305CEE78FF31ull, overlay_right_call},
    {kGlyphMask, 0x00416EF7, 0x78DDC740E43814F2ull, glyph_mask_call},
    {kColouredMirrored, 0x00415C40, 0xAE83B55EECD878C6ull, coloured_whole_call},
    {kColouredMirroredRight, 0x00415D24, 0xCAFF00EA20EA2D75ull, coloured_right_call},
    {kColouredMirroredLeft, 0x00415E29, 0x330F6AC34FD30A16ull, coloured_left_call},
    {kBlendedMirrored, 0x00414539, 0x9C7FF775C17E088Full, blended_mirrored_call},
    {kBlendedRight, 0x00414907, 0x1BD85DDEA880D238ull, blended_right_call},
    {kBlendedMirroredRight, 0x00414CDF, 0x8F2F38CC710A6CC7ull, blended_mirrored_right_call},
    {kBlendedLeft, 0x004150AD, 0x2CB0E004A8262503ull, blended_left_call},
    {kBlendedMirroredLeft, 0x00415481, 0xDC574E91C8B0F9D0ull, blended_mirrored_left_call},
    {0x00417040, 0x004170AD, 0x4BCA2D213D612CB3ull, nullptr},  // FUN_00417040, lignes masquées (deux suites)
    {0x004170B0, 0x004170FC, 0xF5EDB65486EA6F2Dull, nullptr},  // FUN_004170b0, lignes masquées
    {kBuildingTopMirrored, 0x005AE459, 0x04CFC394D7323F8Dull, nullptr},  // chemin rapide dans native.cpp
};

}  // namespace

bool blending_known() { return g_known; }

void overlay_runs(Cpu& cpu, uint32_t image, uint16_t colour, uint8_t clip_x) {
    if (clip_x == 1) overlay_runs_clipped(cpu, image, colour, true);
    else if (clip_x == 2) overlay_runs_clipped(cpu, image, colour, false);
    else runs_whole(cpu, image, colour, false);
}

void draw_glyph_mask(Cpu& cpu, uint32_t image) { runs_whole(cpu, image, get(cpu, kGlyphColour), true); }

void draw_runs_mirrored_coloured(Cpu& cpu, uint32_t image, uint16_t colour, uint8_t clip_x) {
    coloured_mirrored(cpu, image, colour, clip_x);
}

void draw_runs_blended_variant(Cpu& cpu, uint32_t image, uint32_t table, bool mirror, uint8_t clip_x) {
    require_555(cpu);
    if (mirror) {
        if (clip_x == 0) draw_blended_mirrored(cpu, image, table);
        else draw_blended_mirrored_clipped(cpu, image, table, clip_x == 1);
    } else if (clip_x == 0) {
        draw_runs_blended_whole(cpu, image, table);
    } else {
        draw_blended_clipped(cpu, image, table, clip_x == 1);
    }
}

int install_native_blending(Process& p) {
    Cpu& cpu = p.cpu();
    for (const Routine& r : kRoutines)
        if (fingerprint(cpu, r.address, r.end) != r.hash) return 0;
    g_known = true;
    int installed = 0;
    for (const Routine& r : kRoutines)
        if (r.fn) {
            cpu.replace_function(r.address, 0, r.fn);
            ++installed;
        }
    return installed;
}

}  // namespace zeus::win32
