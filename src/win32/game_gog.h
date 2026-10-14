// Adresses de l'exécutable GOG (Zeus.exe, SHA-256 382bcd28…75e90a57) utilisées par l'hôte.
//
// Toutes les adresses du jeu que l'hôte lit, écrit ou intercepte sont ici, une par ligne, avec la
// fonction d'origine qui les emploie (noms FUN_xxxxxxxx de Ghidra). Celles qui désignent du code
// ne sont modifiées qu'après vérification des octets attendus par `matches` ; si un octet diffère
// (autre édition du jeu), l'hôte laisse le code d'origine tel quel. Les variables ne sont lues
// qu'une fois le code qui s'en sert vérifié. Détails et preuves : docs/reverse-engineering.md et
// les commentaires des fichiers qui s'en servent ; toutes sont vérifiées par les journaux d'images
// des scénarios (tools/check_scenarios.py), identiques avec et sans --no-native.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "win32/cpu.h"

namespace zeus::win32::gog {

// Vrai si la mémoire du jeu contient exactement `expected` à partir de `address`.
inline bool matches(Cpu& cpu, uint32_t address, const uint8_t* expected, size_t size) {
    return std::memcmp(cpu.host(address), expected, size) == 0;
}

inline bool matches(Cpu& cpu, uint32_t address, std::initializer_list<uint8_t> expected) {
    return matches(cpu, address, expected.begin(), expected.size());
}

// Valeur immédiate de 1 ou 4 octets (petit-boutiste) inscrite dans une instruction.
inline bool matches_value(Cpu& cpu, uint32_t address, uint32_t value, size_t size) {
    const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                              static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    return matches(cpu, address, bytes, size);
}

// --- Code : fonctions remplacées en natif (native.cpp) ---------------------------------------

constexpr uint32_t kCopyRectangle = 0x00413DC0;       // FUN_00413dc0 : copie d'un rectangle opaque
constexpr uint32_t kDrawRunsBlended = 0x00413E30;     // FUN_00413e30 : image par suites, mélangée
constexpr uint32_t kCopyRectangleKeyed = 0x00416010;  // FUN_00416010 : copie, magenta transparent
constexpr uint32_t kDrawRuns = 0x004161B0;            // FUN_004161b0 : image par suites (et ombre)
constexpr uint32_t kClearFrame = 0x0050B410;          // FUN_0050b410 : efface le tampon d'affichage
constexpr uint32_t kSpriteDispatch = 0x005ACF50;      // FUN_005acf50 : répartiteur de sprites
constexpr uint32_t kStrcmpi = 0x005CC640;             // __strcmpi de la bibliothèque C (VC++ 6)
// Boucle de recherche de clé de FUN_00505770 (lecture de « Zeus eventmsg.txt ») : début et sortie.
constexpr uint32_t kKeySearch = 0x005059F8, kKeySearchEnd = 0x00505A33;

// --- Code : cadencement (user32.cpp) ----------------------------------------------------------

constexpr uint32_t kSimulationDue = 0x005168A0;  // FUN_005168a0 : un tour de simulation est-il dû ?

// --- Code : texte des packs de langue (language.cpp) ------------------------------------------

constexpr uint32_t kDrawGlyph = 0x00508B50;             // FUN_00508b50 : glyphe d'un texte d'une ligne
constexpr uint32_t kPolishException = 0x00508C7F;       // dans FUN_00508b50 : exceptions polonaises
constexpr uint32_t kPanelGlyph = 0x004F59B0;            // FUN_004f59b0 : glyphe d'un texte de panneau
constexpr uint32_t kPanelSubtract = 0x004F5A33;         // dans FUN_004f59b0 : « SUB ECX,0xB »
constexpr uint32_t kPanelPolishException = 0x004F5ACF;  // dans FUN_004f59b0 : exceptions polonaises

// --- Code : écran large (widescreen.cpp), valeurs immédiates du mode 1024x768 -----------------

constexpr uint32_t kMode3Width = 0x0050BD03, kMode3Height = 0x0050BD0D;  // FUN_0050bcc0 : 1024, 768
constexpr uint32_t kPanelX = 0x0058E2CA;        // FUN_0058e280 : panneau de contrôle en x = 838
constexpr uint32_t kFoldedPanelX = 0x0058E2D4;  // FUN_0058e280 : panneau replié en x = 958
constexpr uint32_t kBottomBorder = 0x0058E42F;  // FUN_0058e3e0 : largeur du liseré du bas, 1024
// FUN_0051cc00 (panneau déplié) et FUN_0051cba0 (replié) : rangées et colonnes de la vue (octets).
constexpr uint32_t kViewRows = 0x0051CC29, kViewColumns = 0x0051CC2B;              // 50, 14
constexpr uint32_t kFoldedViewRows = 0x0051CBD3, kFoldedViewColumns = 0x0051CBD5;  // 50, 17
// Comparaisons de la largeur d'écran avec 1024 et de la hauteur avec 768.
constexpr uint32_t kWidthCompares[] = {0x00422D55, 0x005051FA, 0x0051CBC8, 0x0051CC1E, 0x0058E2BE,
                                       0x0058E2F7, 0x0058E3FD, 0x0058E41C, 0x0058EF7C, 0x0059EBFB};
constexpr uint32_t kHeightCompares[] = {0x00505212, 0x00571685};
// FUN_0059ebe0 : retour de l'appel qui dessine le fond de la barre du haut (mode 1024).
constexpr uint32_t kTopBarDrawn = 0x0059EC36;

// --- Variables ---------------------------------------------------------------------------------

// Écran (FUN_0050bcc0, FUN_0050c1b0).
constexpr uint32_t kScreenWidth = 0x00F2A6B4, kScreenHeight = 0x00F2A6B0;
constexpr uint32_t kFrameBuffer = 0x00F9C100;  // adresse du tampon d'affichage, 16 bits par pixel
constexpr uint32_t kRowBytes = 0x00F2A698;     // octets par ligne du tampon
constexpr uint32_t kRowPixels = 0x00F2A6A0;    // pixels par ligne du tampon
constexpr uint32_t kPixelFormat = 0x00F2AB24;  // 0x565 ou 0x555
constexpr uint32_t kNoClear = 0x0063C040;      // non nul : FUN_0050b410 n'efface pas
constexpr uint32_t kPanelPosition = 0x010DFF34;  // x du panneau de contrôle (FUN_0058e280)

// Dessin d'une image : partie visible calculée par FUN_00509c20 (largeur) et FUN_00509cd0
// (hauteur), lue par les routines de dessin, écrite par FUN_005acf50.
constexpr uint32_t kTargetX = 0x00F13A40, kTargetY = 0x00F13A3C;  // premier pixel écrit
constexpr uint32_t kRows = 0x00F13A34;          // lignes à dessiner
constexpr uint32_t kColumns = 0x00F139A4;       // pixels visibles par ligne
constexpr uint32_t kSourceSkip = 0x00F139A0;    // octets de source à sauter après chaque ligne
constexpr uint32_t kTargetSkip = 0x00F1399C;    // octets de destination à sauter après chaque ligne
constexpr uint32_t kImageWidth = 0x00F13A1E;    // largeur de l'image, 16 bits
constexpr uint32_t kImageHeight = 0x00F13A1C;   // hauteur de l'image, 16 bits
constexpr uint32_t kSkippedRows = 0x00F13998;   // lignes de l'image masquées en haut
constexpr uint32_t kSpriteId = 0x00F139E0, kSpriteX = 0x00F139DC, kSpriteY = 0x00F139D8;  // arguments
constexpr uint32_t kSpriteImage = 0x00F13A18, kSpriteKind = 0x00F13A14;
constexpr uint32_t kClipX = 0x00F13991, kClipY = 0x00F13990;  // 0 entière, 1 à 4 rognée, 5 invisible
constexpr uint32_t kSkipLeft = 0x00F139AC, kRight = 0x00F139A8, kVisibleRows = 0x00F13994;
constexpr uint32_t kClipLeft = 0x00F139BC, kClipRight = 0x00F139B8;  // rectangle de dessin
constexpr uint32_t kClipTop = 0x00F139B4, kClipBottom = 0x00F139B0;
constexpr uint32_t kOverlay = 0x0078C570;  // couleur de surimpression, 16 bits
// Lus par FUN_005acf50 en 0x005AD65B pour reconnaître les écrans dont le fond de carte se dessine
// d'un bloc : écran courant (octet), sous-mode, sous-écran, objet dont +4 est l'image du fond.
constexpr uint32_t kScreen = 0x00F2A6DD, kSubmode = 0x00781894, kSubscreen = 0x00FFA180,
                   kBackground = 0x0136F3E8;

// Bibliothèque C : paramètres régionaux (nul tant que setlocale n'est pas appelé, jamais par le jeu).
constexpr uint32_t kLocale = 0x0160BFA4;

// Cadencement (FUN_005168a0).
constexpr uint32_t kGameSpeed = 0x00F2A774;      // vitesse du jeu en pourcentage
constexpr uint32_t kLastTurn = 0x00F9F998;       // instant du dernier tour (timeGetTime)
constexpr uint32_t kSpeedIntervals = 0x0063DA1C;  // 10 intervalles en ms, de 100 % à 10 %

// Texte (FUN_00508b50, FUN_004f59b0).
constexpr uint32_t kCharTable = 0x0063BE2C;  // caractère -> glyphe, 224 octets (0x20 à 0xFF)
constexpr uint32_t kFontGroups = 0x012AFBEC;  // pointeur vers les groupes de polices, 8 octets chacun
constexpr uint32_t kLastFontGroup = 0x012BC820;  // rang du dernier groupe
constexpr uint32_t kLineHeight = 0x00F13960;  // hauteur de ligne de la police courante

}  // namespace zeus::win32::gog
