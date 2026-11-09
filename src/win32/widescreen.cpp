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
// Au-delà de 768 lignes (les hauteurs se comparent aussi à 768, voir kHeightCompares) :
//
//   - le fond du panneau de contrôle est une image de 768 pixels de haut. Juste après son dessin
//     (kPanelDrawn, kPanelOverlayDrawn, kWorldPanelDrawn), l'hôte remplit le dessous avec les
//     six dernières lignes de l'image, qui ne portent que la texture du fond, prises dans un ordre
//     et avec un décalage horizontal variables d'une ligne à l'autre : pas de motif répété visible ;
//   - la carte du monde (FUN_00599da0) copie une fenêtre de son image haute de l'écran moins 30
//     lignes, sans la borner à l'image (738 lignes) comme elle le fait en largeur : l'hôte la
//     borne (kWorldWindowRowsChecked) ;
//   - FUN_005715a0 place la fenêtre d'information en bas d'un écran de 768 lignes (760 - 16 n) et
//     choisit le haut ou le bas selon un seuil de 396 (0x00F2A734) : ces deux valeurs suivent la hauteur, comme
//     elles le font entre 600 et 768.
//
// World map. Its image is 838 x 738, the room the 1024 x 768 mode leaves it, and the game puts it
// at (0, 30) (kWorldOriginX, kWorldOriginY): in a larger screen the city stayed visible to its
// right and below it. The host centres it between the top bar and the control panel
// (kWorldOriginSet); the cities, the routes and the mouse all follow that origin. The game's mouse
// bounds assume the map is at least x0 away from the right edge of the screen (x < width − 2 x0)
// and y0 away from its bottom (y < height − y0): centred vertically, the band at the bottom of the
// map that the mouse does not reach keeps the 30 rows it has in the original mode; horizontally,
// x0 is capped at (width − image) / 3. The room around the map gets the texture of the control
// panel's background, with a short shadow along the map's edges (kWorldImageCopied).
//
// Le reste de la mise en page se calcule depuis la hauteur (la vue de la carte, le liseré du bas
// en hauteur - 9) ou se centre (les fenêtres) : vérifié par captures en 1444x903 et 1324x1023.
//
// La vue fait 60 pixels par colonne et 15 par rangée : seules les largeurs 184 + 60 n et les
// hauteurs 33 + 15 n tombent juste, et la taille demandée est arrondie vers le bas.
// Adresses : win32/game_gog.h ; rien n'est modifié si un seul des octets attendus diffère.

#include <algorithm>
#include <cstdio>
#include <memory>
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
constexpr int kPanelHeight = 768, kPanelWidth = 186, kPanelBorder = 7, kPanelTexture = 6;

// Mélange d'entiers (finaliseur de MurmurHash3) : choix stable d'une ligne et d'un décalage.
uint32_t mix(uint32_t n) {
    n ^= n >> 16;
    n *= 0x85EBCA6Bu;
    n ^= n >> 13;
    n *= 0xC2B2AE35u;
    return n ^ (n >> 16);
}

// Prolonge le fond du panneau sous ses 768 lignes, dans le tampon d'affichage du jeu.
void extend_panel(Cpu& c, int width, int height) {
    const uint32_t buffer = c.read_u32(kFrameBuffer), pitch = static_cast<uint32_t>(width) * 2;
    const int x0 = static_cast<int32_t>(c.read_u32(kPanelPosition));
    if (!buffer || c.read_u32(kScreenWidth) != static_cast<uint32_t>(width) ||
        c.read_u32(kScreenHeight) != static_cast<uint32_t>(height) || x0 < 0 || x0 + kPanelWidth > width)
        return;
    auto row = [&](int y) { return reinterpret_cast<uint16_t*>(c.host(buffer + static_cast<uint32_t>(y) * pitch)) + x0; };
    constexpr int inner = kPanelWidth - kPanelBorder;
    for (int y = kPanelHeight; y < height; ++y) {
        const uint16_t* from = row(kPanelHeight - kPanelTexture + static_cast<int>(mix(2 * y) % kPanelTexture));
        uint16_t* to = row(y);
        const int shift = static_cast<int>(mix(2 * y + 1) % inner);
        // Le liseré de gauche reste en place ; la texture glisse.
        for (int x = 0; x < kPanelBorder; ++x) to[x] = from[x];
        for (int x = 0; x < inner; ++x) to[kPanelBorder + x] = from[kPanelBorder + (x + shift) % inner];
    }
}

// Texture of the control panel's background: its last kPanelTexture rows, without the left border.
constexpr int kTextureWidth = kPanelWidth - kPanelBorder;
using Texture = std::vector<uint16_t>;

// Saved right after the panel's background is drawn, while those rows are known to be fresh.
void capture_texture(Cpu& c, int width, int height, Texture& texture) {
    const uint32_t buffer = c.read_u32(kFrameBuffer), pitch = static_cast<uint32_t>(width) * 2;
    const int x0 = static_cast<int32_t>(c.read_u32(kPanelPosition));
    if (!buffer || c.read_u32(kScreenWidth) != static_cast<uint32_t>(width) ||
        c.read_u32(kScreenHeight) != static_cast<uint32_t>(height) || height < kPanelHeight || x0 < 0 ||
        x0 + kPanelWidth > width)
        return;
    texture.resize(static_cast<size_t>(kPanelTexture * kTextureWidth));
    for (int r = 0; r < kPanelTexture; ++r) {
        const auto* from = reinterpret_cast<const uint16_t*>(
                               c.host(buffer + static_cast<uint32_t>(kPanelHeight - kPanelTexture + r) * pitch)) +
                           x0 + kPanelBorder;
        std::copy(from, from + kTextureWidth, texture.begin() + r * kTextureWidth);
    }
}

// Fills the room around the world map, between the top bar and the panel, with that texture.
void frame_world_map(Cpu& c, int width, int height, Texture& texture) {
    const uint32_t buffer = c.read_u32(kFrameBuffer), pitch = static_cast<uint32_t>(width) * 2;
    if (!buffer || c.read_u32(kScreenWidth) != static_cast<uint32_t>(width) ||
        c.read_u32(kScreenHeight) != static_cast<uint32_t>(height))
        return;
    if (texture.empty()) capture_texture(c, width, height, texture);
    if (texture.empty()) return;
    const int x0 = static_cast<int32_t>(c.read_u32(kWorldOriginX)), y0 = static_cast<int32_t>(c.read_u32(kWorldOriginY));
    const int w = static_cast<int32_t>(c.read_u32(kWorldImageWidth)), h = static_cast<int32_t>(c.read_u32(kWorldImageHeight));
    const int right = std::min<int>(width, static_cast<int32_t>(c.read_u32(kPanelPosition)));
    for (int y = kTopBarHeight; y < height; ++y) {
        uint16_t* row = reinterpret_cast<uint16_t*>(c.host(buffer + static_cast<uint32_t>(y) * pitch));
        const uint16_t* from = texture.data() + static_cast<int>(mix(3 * y) % kPanelTexture) * kTextureWidth;
        const int shift = static_cast<int>(mix(3 * y + 1) % kTextureWidth);
        const bool beside = y >= y0 && y < y0 + h;  // a row of the map: only its two sides
        for (int x = 0; x < right; ++x) {
            if (beside && x == x0) x = x0 + w;
            if (x >= right) break;
            row[x] = from[(x + shift) % kTextureWidth];
        }
    }
    // A shadow a few pixels wide around the map, so that its edge stands out from the texture.
    const bool rgb565 = c.read_u32(kPixelFormat) == 0x565;
    auto darken = [rgb565](uint16_t p, int percent) {
        const int gbits = rgb565 ? 6 : 5, gmask = (1 << gbits) - 1;
        const int r = (p >> (5 + gbits)) & 31, g = (p >> 5) & gmask, b = p & 31;
        return static_cast<uint16_t>(((r * percent / 100) << (5 + gbits)) | ((g * percent / 100) << 5) | (b * percent / 100));
    };
    constexpr int kShadow[] = {45, 60, 75, 88};  // brightness kept, from the map's edge outwards
    for (int d = 1; d <= 4; ++d) {
        const int left = x0 - d, top = y0 - d, rgt = x0 + w - 1 + d, bottom = y0 + h - 1 + d;
        for (int y = std::max(top, kTopBarHeight); y <= std::min(bottom, height - 1); ++y) {
            uint16_t* row = reinterpret_cast<uint16_t*>(c.host(buffer + static_cast<uint32_t>(y) * pitch));
            if (y == top || y == bottom) {  // above or below the map
                for (int x = std::max(left, 0); x <= std::min(rgt, right - 1); ++x) row[x] = darken(row[x], kShadow[d - 1]);
            } else {  // on its left and right, when there is room
                if (left >= 0) row[left] = darken(row[left], kShadow[d - 1]);
                if (rgt < right) row[rgt] = darken(row[rgt], kShadow[d - 1]);
            }
        }
    }
}

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
    changes.push_back({{kInfoWindowBottom, 760, 4}, static_cast<uint32_t>(height - 8)});      // fenêtre d'information
    changes.push_back({{kInfoWindowMiddle, 396, 4}, static_cast<uint32_t>(height / 2 + 12)});

    for (const auto& [site, value] : changes) {
        if (!matches_value(cpu, site.address, site.expected, static_cast<size_t>(site.size))) {
            std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", site.address);
            return false;
        }
    }
    if (!matches(cpu, kWorldWindowRowsSet, {0x89, 0x15, 0xD0, 0x39, 0xF1, 0x00})) {  // MOV [0x00F139D0],EDX
        std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", kWorldWindowRowsSet);
        return false;
    }
    if (!matches(cpu, kWorldOriginSet, {0xA1, 0x34, 0xFF, 0x0D, 0x01})) {  // MOV EAX,[0x010DFF34]
        std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", kWorldOriginSet);
        return false;
    }
    if (!matches(cpu, kWorldImageCopied, {0x6A, 0x01})) {  // PUSH 1
        std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", kWorldImageCopied);
        return false;
    }
    for (uint32_t address : {kTopBarDrawn, kPanelDrawn, kPanelOverlayDrawn, kWorldPanelDrawn}) {
        if (!matches(cpu, address - 5, {0xE8})) {  // CALL
            std::fprintf(stderr, "[hôte] écran large : exécutable inconnu (%#x), mode non modifié\n", address);
            return false;
        }
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
    auto texture = std::make_shared<Texture>();
    for (uint32_t address : {kPanelDrawn, kPanelOverlayDrawn, kWorldPanelDrawn})
        cpu.add_code_hook(address, [width, height, texture](Cpu& c) {
            capture_texture(c, width, height, *texture);
            if (height > kPanelHeight) extend_panel(c, width, height);
        });
    // World map centred between the top bar and the panel, with its surroundings filled.
    cpu.add_code_hook(kWorldOriginSet, [width, height](Cpu& c) {
        if (c.read_u32(kScreenWidth) != static_cast<uint32_t>(width) || c.read_u32(kScreenHeight) != static_cast<uint32_t>(height))
            return;
        const int w = static_cast<int32_t>(c.read_u32(kWorldImageWidth)), h = static_cast<int32_t>(c.read_u32(kWorldImageHeight));
        const int area = static_cast<int32_t>(c.read_u32(kPanelPosition));
        if (w <= 0 || h <= 0 || area <= 0 || area > width) return;
        const int x0 = std::max(0, std::min((area - w) / 2, (width - w) / 3));
        const int y0 = kTopBarHeight + std::max(0, (height - kTopBarHeight - h) / 2);
        c.write_u32(kWorldOriginX, static_cast<uint32_t>(x0));
        c.write_u32(kWorldOriginY, static_cast<uint32_t>(y0));
    });
    cpu.add_code_hook(kWorldImageCopied, [width, height, texture](Cpu& c) { frame_world_map(c, width, height, *texture); });
    if (height > kPanelHeight) {
        // La carte du monde ne copie pas plus de lignes que son image n'en a (738) : au-delà, la
        // copie lisait hors de l'image.
        cpu.add_code_hook(kWorldWindowRowsChecked, [](Cpu& c) {
            const int32_t image = static_cast<int32_t>(c.read_u32(kWorldImageHeight));
            if (image > 0 && static_cast<int32_t>(c.read_u32(kWindowRows)) > image) c.write_u32(kWindowRows, static_cast<uint32_t>(image));
        });
    }
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
