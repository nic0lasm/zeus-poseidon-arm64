// Écran large : remplace en mémoire le mode 1024x768 du jeu par un mode plus grand.
//
// Le jeu connaît trois modes (640x480, 800x600, 1024x768) et range la taille courante en
// 0x00F2A6B4 et 0x00F2A6B0. Sa mise en page est en partie calculée depuis ces variables, en
// partie choisie en comparant la largeur à 640, 800 ou 1024. On fait donc passer la nouvelle
// taille pour « le mode 1024 » partout où le jeu compare, et on recalcule les quelques valeurs
// qu'il a fixées en dur pour ce mode :
//
//   - FUN_0050bcc0 : taille du mode 3 ;
//   - comparaisons avec 1024 ou 768 dans les fonctions qui lisent la taille d'écran ;
//   - FUN_0058e280 : position du panneau de contrôle, largeur - 186, et panneau replié,
//     largeur - 66 ;
//   - FUN_0058e3e0 : largeur du liseré du bas de l'écran ;
//   - FUN_0051cc00 et FUN_0051cba0 : nombre de colonnes et de rangées de tuiles de la vue de la
//     carte, passé à FUN_00449e50 (panneau déplié : 14 x 50 ; replié : 17 x 50).
//
// Le fond de la barre du haut est une image de 838 pixels de large, dessinée par FUN_0059ebe0.
// Juste après ce dessin (retour d'appel en 0x0059EC36, branche du mode 1024), l'hôte prolonge l'image jusqu'au
// panneau en la répétant en miroir dans le tampon d'affichage du jeu (adresse en 0x00F9C100).
//
// La vue fait 60 pixels par colonne et 15 par rangée : seules les largeurs 184 + 60 n et les
// hauteurs 33 + 15 n tombent juste, et la taille demandée est arrondie vers le bas.
// Adresses : win32/game_gog.h ; rien n'est modifié si un seul des octets attendus diffère.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "win32/game_gog.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

struct Site {
    uint32_t address;
    uint32_t expected;
    int size;  // octets
};

constexpr int kTopBarWidth = 838, kTopBarHeight = 30, kTopBarRepeat = 500;

void write(Cpu& cpu, const Site& s, uint32_t value) {
    if (s.size == 4) cpu.write_u32(s.address, value);
    else cpu.write_u8(s.address, static_cast<uint8_t>(value));
}

}  // namespace

bool apply_wide_mode(Process& p, int& width, int& height) {
    Cpu& cpu = p.cpu();
    const int columns = (width - 184) / 60, rows = (height - 33) / 15 + 1;
    if (columns < 14 || rows < 50 || columns + 3 > 127 || rows > 127) {
        std::fprintf(stderr, "[hôte] écran large : taille hors limites (au moins 1024x768)\n");
        return false;
    }
    width = 184 + 60 * columns;
    height = 33 + 15 * (rows - 1);

    std::vector<std::pair<Site, uint32_t>> changes = {
        {{kMode3Width, 1024, 4}, static_cast<uint32_t>(width)},    // taille du mode 3
        {{kMode3Height, 768, 4}, static_cast<uint32_t>(height)},
        {{kPanelX, 838, 4}, static_cast<uint32_t>(width - 186)},  // panneau de contrôle
        {{kFoldedPanelX, 958, 4}, static_cast<uint32_t>(width - 66)},   // panneau replié
        {{kBottomBorder, 1024, 4}, static_cast<uint32_t>(width)},       // liseré du bas
        {{kViewRows, 50, 1}, static_cast<uint32_t>(rows)},          // vue, panneau déplié
        {{kViewColumns, 14, 1}, static_cast<uint32_t>(columns)},
        {{kFoldedViewRows, 50, 1}, static_cast<uint32_t>(rows)},          // vue, panneau replié
        {{kFoldedViewColumns, 17, 1}, static_cast<uint32_t>(columns + 3)},
    };
    for (uint32_t address : kWidthCompares) changes.push_back({{address, 1024, 4}, static_cast<uint32_t>(width)});
    for (uint32_t address : kHeightCompares) changes.push_back({{address, 768, 4}, static_cast<uint32_t>(height)});

    for (const auto& [site, value] : changes) {
        if (!matches_value(cpu, site.address, site.expected, static_cast<size_t>(site.size))) {
            std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", site.address);
            return false;
        }
    }
    if (!matches(cpu, kTopBarDrawn - 5, {0xE8})) {  // CALL
        std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", kTopBarDrawn);
        return false;
    }
    for (const auto& [site, value] : changes) write(cpu, site, value);
    cpu.add_code_hook(kTopBarDrawn, [width](Cpu& c) {
        // Le tampon d'affichage du jeu fait la taille de l'écran, deux octets par pixel.
        const uint32_t buffer = c.read_u32(kFrameBuffer), pitch = static_cast<uint32_t>(width) * 2;
        const int end = std::min<int>(width, static_cast<int32_t>(c.read_u32(kPanelPosition)));
        if (!buffer || c.read_u32(kScreenWidth) != static_cast<uint32_t>(width)) return;
        for (int y = 0; y < kTopBarHeight; ++y) {
            uint16_t* row = reinterpret_cast<uint16_t*>(c.host(buffer + static_cast<uint32_t>(y) * pitch));
            for (int x = kTopBarWidth; x < end; ++x) {
                // Aller et retour dans les 500 derniers pixels de l'image : pas de raccord visible.
                const int m = (x - kTopBarWidth) % (2 * kTopBarRepeat);
                row[x] = row[m < kTopBarRepeat ? kTopBarWidth - 1 - m : kTopBarWidth - 2 * kTopBarRepeat + m];
            }
        }
    });
    // Libellés « 1024 x 768 » de la fenêtre des options d'affichage et de l'aide de la touche F7 :
    // ils donnent la vraie taille, à longueur égale (une largeur de quatre chiffres).
    const std::string size = std::to_string(width) + " x " + std::to_string(height);
    const std::string compact = std::to_string(width) + "x" + std::to_string(height);
    if (size.size() == 10) {
        p.add_file_patch({"zeus_text.eng", "1024 x 768", size});
        p.add_file_patch({"zeus_mm.eng", "1024x768", compact});
    }
    return true;
}

}  // namespace zeus::win32
