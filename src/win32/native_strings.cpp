// Dessin d'une chaîne d'une ligne, réécrit pour l'hôte (docs/engine/interface.md §7.4 et §12,
// étapes 3 et 8) :
//
//   - FUN_00508800(texte, x, y, police, couleur, drapeau) : le moteur des chaînes, appelé par
//     FUN_00508670 (centrée), FUN_00508d40, FUN_00508d90 et 37 appelants. Il fixe l'espace et la
//     hauteur de ligne de la police, puis dessine chaque caractère avec FUN_00508b50 (une, deux ou
//     trois fois selon le style : contour 0x00F13978, ombre 0x00F13974) ; « _ » est une espace et
//     « # » prend les caractères du texte préparé en 0x00F27630 (FUN_00508d10).
//   - FUN_00508b50(police, caractère - 0x20, x, y) : un glyphe, remonté pour les lettres
//     accentuées, et sa largeur.
//
// En jeu réel (working-city, temps du fil de l'émulateur), FUN_00508800 prenait 6 % du temps :
// chaque glyphe coûtait une boucle émulée et l'entrée du répartiteur natif (environ 140 ns).
// Un seul chemin rapide par chaîne (Cpu::add_fast_path) calcule ici tous les glyphes, vérifie
// avant la première écriture que le répartiteur natif sait tous les dessiner (sprite_handled),
// puis les dessine dans l'ordre de l'original ; sinon il rend la main sans avoir rien écrit et
// l'original fait tout. Des variables qu'écrivent ces fonctions, le répartiteur ne lit que la
// couleur kGlyphColour (0x00F1397C, pour les masques de glyphes), écrite avant chaque glyphe comme
// dans l'original ; ce que lit sprite_handled n'est écrit par aucun dessin. EAX vaut 0 comme dans l'original ; les
// emplacements d'arguments que l'original réutilise (pointeur du texte, octet bas de la couleur
// et du drapeau) reçoivent les mêmes valeurs.
//
// Pack de langue (language.cpp) : il remplace en 0x00508C7F l'instruction des exceptions
// polonaises par des NOP et, à l'entrée de chaque glyphe, réécrit la hauteur de ligne selon le
// rang de la police. Le pack est posé après les fonctions natives : sa présence se lit donc à
// chaque appel dans ces octets, et le chemin rapide fait alors ce que fait l'original modifié.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si ses octets sont ceux attendus
// (empreinte FNV-1a, comparée avant toute modification).

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

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
int16_t s16(Cpu& cpu, uint32_t address) {
    int16_t value;
    std::memcpy(&value, cpu.host(address), 2);
    return value;
}
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }
void put16(Cpu& cpu, uint32_t address, uint16_t value) { std::memcpy(cpu.host(address), &value, 2); }

// Hauteurs de ligne du pack de langue par rang de police (language.cpp, kLineHeights).
constexpr int32_t kPackLineHeights[17] = {10, 16, 16, 16, 16, 16, 29, 29, 12, 16, 12, 11, 11, 11, 11, 16, 10};

// Ce que le crochet du pack écrit dans kLineHeight à l'entrée d'un glyphe, ou -1 s'il n'écrit rien.
int32_t pack_line_height(Cpu& cpu, uint32_t font) {
    const uint32_t groups = u32(cpu, kFontGroups);
    const uint32_t last = u32(cpu, kLastFontGroup) + 1;
    const uint32_t count = last < 17 ? last : 17;
    for (uint32_t i = 0; i < count; ++i)
        if (u32(cpu, groups + i * 8 + 4) == font) return kPackLineHeights[i];
    return -1;
}

// Entrée de 0x48 octets de l'image `id` (banque × 16384 + numéro, divisions signées) ; un numéro
// au-delà du dernier de la banque lit l'entrée 0 (0x00508BC8, comparaison signée).
uint32_t image_entry(Cpu& cpu, int32_t id) {
    const int32_t bank = id / 16384, number = id % 16384;
    const uint32_t base = kSpriteBanks + static_cast<uint32_t>(bank) * kImageFileStride;
    const uint32_t table = u32(cpu, base + 0x1000);
    return number <= i32(cpu, base + 0xDC34) ? table + static_cast<uint32_t>(number) * 0x48 : table;
}

// Caractères (code) que FUN_00508b50 remonte de 2 pixels : les lettres polonaises (0x00508C01).
bool polish(uint32_t code) {
    switch (code) {
        case 0xB9: case 0xA5: case 0xEA: case 0xCA: case 0xE6: case 0xC6: case 0xF1: case 0xD1: case 0xB3:
        case 0xA3: case 0xF3: case 0xD3: case 0x9F: case 0x8F: case 0xBF: case 0xAF: case 0x9C: case 0x8C:
            return true;
        default:
            return false;
    }
}

struct Glyph {
    int32_t id, x, y;
    uint16_t colour;  // valeur de kGlyphColour pendant le dessin
};

// État que FUN_00508800 lit et écrit, gardé ici jusqu'à la fin de l'appel.
struct Render {
    Cpu& cpu;
    const uint8_t* m;
    bool pack;
    uint32_t font;
    int32_t line_height;   // kLineHeight
    uint16_t colour;       // kGlyphColour
    uint32_t drawn_glyph;  // kDrawnImage
    std::vector<Glyph>& glyphs;

    // FUN_00508b50(police, argument, x, y) : le glyphe du caractère (octet bas de l'argument + 0x20)
    // à dessiner, et sa largeur (champ +0x14 de l'image, sans le +1).
    int32_t glyph(uint32_t argument, int32_t x, int32_t y) {
        if (pack) {  // crochet du pack, à l'entrée de la fonction
            const int32_t height = pack_line_height(cpu, font);
            if (height >= 0) line_height = height;
        }
        const uint32_t ch = argument & 0xFF;
        drawn_glyph = m[kCharTable + ch];
        if (drawn_glyph == 0) return 0;  // jamais atteint : l'appelant a testé le glyphe
        const int32_t id = static_cast<int32_t>(drawn_glyph + font - 1);
        const uint32_t entry = image_entry(cpu, id);
        int32_t lift = s16(cpu, entry + 0x16) - line_height;
        if (lift < 0) lift = 0;
        if (ch < 0x60 || ch == 0xC7 || ch == 0xE7) lift = 0;
        if (!pack && polish(ch + 0x20)) lift = 2;
        glyphs.push_back({id, x, y - 3 - lift, colour});
        return s16(cpu, entry + 0x14);
    }
};

std::vector<Glyph>& glyph_list() {
    static std::vector<Glyph> list;  // réutilisé : les chemins rapides s'exécutent sur le fil de l'émulateur
    list.clear();
    return list;
}

// FUN_00508800(texte, x, y, police, couleur, drapeau).
std::optional<uint32_t> render_call(Cpu& cpu) {
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    const uint8_t* m = cpu.host(0);
    const uint32_t font = cpu.arg(3);
    int32_t x = static_cast<int32_t>(cpu.arg(1));
    const int32_t y = static_cast<int32_t>(cpu.arg(2));
    uint32_t text = cpu.arg(0), colour_slot = cpu.arg(4), flag_slot = cpu.arg(5);
    bool text_moved = false, colour_moved = false, flag_moved = false;

    // Espace et hauteur de ligne de la police (0x0050881B à 0x005088CB) ; un pixel de plus après
    // chaque glyphe de la police 0. Les groupes 6, 7 et 16 sont lus dans le groupe 0 quand la
    // table est trop courte.
    const uint32_t groups = u32(cpu, kFontGroups);
    const int32_t last = i32(cpu, kLastFontGroup);
    auto first = [&](int32_t g) { return u32(cpu, groups + (last >= g ? static_cast<uint32_t>(g) * 8 : 0) + 4); };
    int32_t space = 6, height = 0x0B, extra = 0;
    if (font == first(6)) {
        space = 8, height = 0x17;
    } else if (font == first(7)) {
        space = 8, height = 0x18;
    } else if (font == u32(cpu, groups + 4)) {
        space = 4, height = 9, extra = 1;
    } else if (font == first(16)) {
        space = 4, height = 7;
    }

    Render r{cpu, m, *cpu.host(kPolishException) != 0xB9, font, height, static_cast<uint16_t>(colour_slot),
             u32(cpu, kDrawnImage), glyph_list()};
    const uint16_t colour = r.colour;  // BX
    const bool from_prepared = u32(cpu, kPreparedActive) == 1;
    const bool keep_spaces = m[kTextNoCollapse] != 0, collapse = m[kTextCollapse] != 0;
    const uint32_t mark = u32(cpu, kTextMarks + u32(cpu, kTextField) * 4);
    // Style : 0x00F13970 non nul, un seul dessin ; sinon contour (0x00F13978) ou ombre (0x00F13974).
    const bool plain = u32(cpu, kTextPlain) != 0;
    const uint32_t outline = u32(cpu, kTextOutline), shadow = u32(cpu, kTextShadow);
    uint32_t cursor = u32(cpu, kTextCursor), count = u32(cpu, kTextCount), prepared = u32(cpu, kPreparedIndex);
    uint32_t found = u32(cpu, kTextMarkFound);
    std::optional<uint32_t> mark_width, mark_cursor, mark_line;

    // FUN_00508d10 : caractère suivant du texte préparé ; à sa fin, une espace et l'indice remis à 0.
    auto next_prepared = [&]() -> uint8_t {
        const uint8_t c = m[kPreparedText + prepared];
        if (c == 0) {
            prepared = 0;
            return ' ';
        }
        ++prepared;
        return c;
    };

    uint8_t al = m[text];
    while (al != 0) {
        flag_slot = (flag_slot & ~0xFFu) | al, flag_moved = true;  // 0x005088EC
        uint8_t c = al;
        int32_t advance = 0;
        bool shown = true;
        if (c == '#') c = from_prepared ? next_prepared() : ' ';
        if (c == '_') c = ' ';
        else if (c < 0x20) shown = false;  // 0x00508959 : rien, et pas d'avance
        if (shown) {
            const uint8_t ch = static_cast<uint8_t>(c - 0x20);
            colour_slot = (colour_slot & ~0xFFu) | ch, colour_moved = true;  // 0x00508908
            if (m[kCharTable + ch] == 0) {
                advance = space;
            } else {
                int32_t width;
                if (plain) {
                    r.colour = colour;
                    width = r.glyph(colour_slot, x, y);
                } else if (outline != 0) {  // 0x00508968 : en couleur 0 dessus et dessous
                    r.colour = 0;
                    r.glyph(colour_slot, x, y - 1);
                    r.colour = 0;
                    r.glyph(colour_slot, x, y + 1);
                    r.colour = colour;
                    width = r.glyph(colour_slot, x, y);
                } else if (shadow != 0) {  // 0x005089C4 : en couleur 0 en bas à droite
                    r.colour = 0;
                    r.glyph(colour_slot, x + 1, y + 1);
                    r.colour = colour;
                    width = r.glyph(colour_slot, x - 1, y);
                } else {
                    r.colour = colour;
                    width = r.glyph(colour_slot, x, y);
                }
                advance = width + extra;
            }
            // 0x00508A28 : position du curseur de saisie, la première fois que le compte l'atteint.
            if (count == mark && found == 0) {
                mark_width = static_cast<uint32_t>(advance);
                mark_cursor = cursor;
                mark_line = u32(cpu, kTextLine);
                found = 1;
            }
            cursor += static_cast<uint32_t>(advance);
            x += advance;
        }
        // 0x00508A7B : caractère suivant.
        if (prepared != 0) {
            al = next_prepared();
        } else {
            al = m[++text];
            text_moved = true;
        }
        ++count;
        // Deux espaces de suite n'en font qu'une (0x00508AB7).
        if (!keep_spaces && collapse && static_cast<uint8_t>(flag_slot) == al && (al == ' ' || al == '_')) {
            cursor -= static_cast<uint32_t>(advance);
            x -= advance;
        }
    }

    for (const Glyph& g : r.glyphs)
        if (!sprite_handled(cpu, kSpriteBanks, g.id, g.x, g.y, 0, 0)) return std::nullopt;
    for (const Glyph& g : r.glyphs) {
        put16(cpu, kGlyphColour, g.colour);
        draw_sprite(cpu, kSpriteBanks, g.id, g.x, g.y, 0, 0);
    }

    if (found == 0) {  // 0x00508AED
        mark_width = 4u;
        mark_cursor = cursor;
        mark_line = u32(cpu, kTextLine);
    }
    if (mark_width) {
        put32(cpu, kTextMarkWidth, *mark_width);
        put32(cpu, kTextMarkCursor, *mark_cursor);
        put32(cpu, kTextMarkLine, *mark_line);
        put32(cpu, kTextMarkFound, 1);
    }
    put32(cpu, kLineHeight, static_cast<uint32_t>(r.line_height));
    put16(cpu, kGlyphColour, r.colour);
    put32(cpu, kDrawnImage, r.drawn_glyph);
    put32(cpu, kTextCursor, cursor + static_cast<uint32_t>(space));
    put32(cpu, kTextCount, count);
    put32(cpu, kPreparedActive, 0);
    put32(cpu, kPreparedIndex, 0);
    *cpu.host(kTextNoCollapse) = 0;
    put32(cpu, kTextEndFlag, 0);
    if (text_moved) put32(cpu, esp + 4, text);
    if (colour_moved) put32(cpu, esp + 0x14, colour_slot);
    if (flag_moved) put32(cpu, esp + 0x18, flag_slot);
    return 0u;
}

uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t a = start; a < end; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_strings(Process& p) {
    Cpu& cpu = p.cpu();
    // FUN_00508800, FUN_00508b50 et FUN_00508d10, d'un seul tenant.
    if (fingerprint(cpu, kDrawString, kDrawStringEnd) != kDrawStringHash) return 0;
    cpu.add_fast_path(kDrawString, 0, 5, render_call);  // « PUSH ECX ; MOV AL,[ESP+0x1C] »
    return 1;
}

}  // namespace zeus::win32
