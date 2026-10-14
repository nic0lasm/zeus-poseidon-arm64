// Pack de langue : adapte en mémoire l'affichage du texte de l'exécutable GOG.
//
// L'exécutable GOG est une compilation pour l'Europe centrale : sa table caractère -> glyphe
// suit le codage Windows-1250, et le placement vertical des lettres accentuées est réglé pour
// le polonais. Les fichiers de polices, eux, portent les accents d'Europe de l'Ouest dans les
// deux éditions. Pour afficher une langue d'Europe de l'Ouest il faut donc reprendre trois
// choses de l'exécutable de cette langue, sans toucher à aucun fichier :
//
//   1. la table caractère -> glyphe (224 octets, caractères 0x20 à 0xFF), fournie par le pack ;
//   2. la hauteur de ligne de chaque police, dont dépend la remontée des lettres accentuées :
//      le glyphe est remonté de (sa hauteur - hauteur de ligne) ;
//   3. l'absence de la liste d'exceptions polonaises, qui remonte de 2 pixels des caractères
//      comme ê, œ ou ñ.
//
// Adresses de l'exécutable GOG (win32/game_gog.h) : table en 0x0063BE2C ; groupes de polices
// dans le tableau pointé par 0x012AFBEC (8 octets par groupe, identifiant d'image à +4,
// dernier rang en 0x012BC820). Deux routines dessinent un glyphe :
//   - FUN_00508b50 (textes d'une ligne) lit la hauteur de ligne en 0x00F13960 et applique les
//     exceptions en 0x00508C7F ;
//   - FUN_004f59b0 (textes des panneaux) retranche 11 en 0x004F5A33, hauteur du glyphe dans
//     ECX, et applique les exceptions en 0x004F5ACF.
// Valeurs de l'exécutable français 2.1 : FUN_005760a9 pour la première routine ; FUN_0055bf09
// pour la seconde, qui retranche 11 pour la police de rang 13 et 16 pour les autres.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

#include "win32/game_gog.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

// Hauteur de ligne par rang de police dans Zeus_Fonts.sg3 (0 : SPR_FONTS … 16 : SPR_FONTS_DKMESSAGE).
constexpr int kLineHeights[17] = {10, 16, 16, 16, 16, 16, 29, 29, 12, 16, 12, 11, 11, 11, 11, 16, 10};

// Rang d'une police dans Zeus_Fonts.sg3 d'après l'identifiant de sa première image, ou -1.
int font_rank(Cpu& c, uint32_t font) {
    const uint32_t groups = c.read_u32(kFontGroups);
    const uint32_t count = std::min<uint32_t>(c.read_u32(kLastFontGroup) + 1, 17);
    for (uint32_t i = 0; i < count; ++i)
        if (c.read_u32(groups + i * 8 + 4) == font) return static_cast<int>(i);
    return -1;
}

}  // namespace

void apply_language_pack(Process& p, const std::filesystem::path& pack) {
    std::ifstream in(pack / "table-caracteres.bin", std::ios::binary);
    const std::vector<uint8_t> table((std::istreambuf_iterator<char>(in)), {});
    if (table.empty()) return;  // pack sans adaptation du texte
    Cpu& cpu = p.cpu();
    // On ne touche qu'à l'exécutable connu : mêmes octets aux trois endroits modifiés.
    const bool known = table.size() == 224 && matches(cpu, kCharTable + 'A' - 0x20, {27}) &&
                       matches(cpu, kDrawGlyph, {0x51, 0x8B, 0x44, 0x24, 0x0C}) &&
                       matches(cpu, kPolishException, {0xB9, 0x02, 0x00, 0x00, 0x00}) &&
                       matches(cpu, kPanelGlyph, {0x53, 0x8B, 0x5C, 0x24, 0x0C}) &&
                       matches(cpu, kPanelSubtract, {0x83, 0xE9, 0x0B}) &&
                       matches(cpu, kPanelPolishException, {0xB9, 0x02, 0x00, 0x00, 0x00});
    if (!known) {
        std::fprintf(stderr, "[hôte] pack de langue : exécutable inconnu, texte non adapté\n");
        return;
    }
    cpu.write(kCharTable, table.data(), table.size());
    const uint8_t nops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
    cpu.write(kPolishException, nops, sizeof nops);
    cpu.write(kPanelPolishException, nops, sizeof nops);
    // À l'entrée du dessin d'un glyphe (police en premier argument), la hauteur de ligne de
    // l'édition d'origine remplace celle que le jeu vient de fixer.
    cpu.add_code_hook(kDrawGlyph, [](Cpu& c) {
        const int rank = font_rank(c, c.arg(0));
        if (rank >= 0) c.write_u32(kLineHeight, static_cast<uint32_t>(kLineHeights[rank]));
    });
    // Textes des panneaux : le jeu va retrancher 11 à la hauteur du glyphe ; l'édition d'origine
    // retranche 16, sauf pour la police de rang 13.
    auto panel_font = std::make_shared<uint32_t>(0);
    cpu.add_code_hook(kPanelGlyph, [panel_font](Cpu& c) { *panel_font = c.arg(0); });
    cpu.add_code_hook(kPanelSubtract, [panel_font](Cpu& c) {
        if (font_rank(c, *panel_font) != 13) c.set_reg(Cpu::Reg::ECX, c.reg(Cpu::Reg::ECX) - 5);
    });
}

}  // namespace zeus::win32
