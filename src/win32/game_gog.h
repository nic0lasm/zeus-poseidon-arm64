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
constexpr uint32_t kDrawRunsLeft = 0x004163B0;        // FUN_004163b0 : la même, rognée à gauche
constexpr uint32_t kDrawRunsRight = 0x004165D0;       // FUN_004165d0 : la même, rognée à droite
constexpr uint32_t kDrawRunsMirrored = 0x00415490;      // FUN_00415490 : image par suites retournée
constexpr uint32_t kDrawRunsMirroredLeft = 0x004158E0;  // FUN_004158e0 : la même, rognée à gauche
constexpr uint32_t kDrawRunsMirroredRight = 0x00415680; // FUN_00415680 : la même, rognée à droite
constexpr uint32_t kCopyRowsKeyed = 0x00415F50;         // FUN_00415f50 : copie à clé de lignes entières
constexpr uint32_t kGroundLeft = 0x004377B0, kGroundLeftEnd = 0x00438050;    // FUN_004377b0 : losange rogné à gauche
constexpr uint32_t kGroundRight = 0x00438A90, kGroundRightEnd = 0x00439340;  // FUN_00438a90 : losange rogné à droite
// FUN_0043ff10 à FUN_00441e50 : socles de 2 à 6 cases, réécrits dans le répartiteur (native.cpp).
constexpr uint32_t kBases = 0x0043FF10, kBasesEnd = 0x004432F8;
constexpr uint32_t kBaseOffset = 0x00F29F74;  // ajouté à l'adresse de chaque losange d'un socle (rôle inconnu)
constexpr uint32_t kClearFrame = 0x0050B410;          // FUN_0050b410 : efface le tampon d'affichage
constexpr uint32_t kSpriteDispatch = 0x005ACF50;      // FUN_005acf50 : répartiteur de sprites
constexpr uint32_t kBuildingTop = 0x005ADF00;         // FUN_005adf00 : partie haute d'un bâtiment
constexpr uint32_t kStrcmpi = 0x005CC640;             // __strcmpi de la bibliothèque C (VC++ 6)
// Boucle de recherche de clé de FUN_00505770 (lecture de « Zeus eventmsg.txt ») : début et sortie.
constexpr uint32_t kKeySearch = 0x005059F8, kKeySearchEnd = 0x00505A33;

// --- Code : boucle de dessin de la ville (native_draw.cpp) ------------------------------------

// FUN_00458170 parcourt les cases visibles deux fois : le sol, puis ce qui s'y dresse.
constexpr uint32_t kCityDraw = 0x00458170;    // FUN_00458170 : dessin de la ville
constexpr uint32_t kGroundLoop = 0x0045829D;  // dans FUN_00458170 : tête de la boucle du sol (une case)
constexpr uint32_t kGroundCall = 0x0045862F;  // dans FUN_00458170 : appel de FUN_00457900 pour la case
constexpr uint32_t kGroundRowEnd = 0x0045868C;  // dans FUN_00458170 : fin d'une rangée de cases
constexpr uint32_t kGroundTile = 0x00457900;  // FUN_00457900(case, x, y, rangée impaire) : sol d'une case
constexpr uint32_t kGroundTileEnd = 0x00458114;  // fin de FUN_00457900 et de ses tables de sauts
constexpr uint32_t kBuildingJumps = 0x0045A254, kBuildingJumpsEnd = 0x0045A2E1;  // table de FUN_00458170
constexpr uint32_t kAlwaysTrue = 0x0055F3D0;  // FUN_0055f3d0 : méthode +0x18 du mode de vue 0, « vrai »
constexpr uint32_t kDrawGround = 0x00435070, kDrawGroundEnd = 0x00436249;  // FUN_00435070 : sol en losange
// Second parcours de FUN_00458170 : ce qui se dresse sur chaque case (native_draw.cpp).
constexpr uint32_t kObjectsCell = 0x00458792;      // case dans la grille (EDI rangée, ECX colonne)
constexpr uint32_t kAfterAmbient = 0x00458D7A;     // après les compteurs d'ambiance
constexpr uint32_t kAfterObject = 0x004594FA;      // après l'objet de la case d'ancrage (EBP = y)
constexpr uint32_t kAfterAnimation = 0x00459CE2;   // avant FUN_0044f320 (EBX bâtiment, EDI x, EBP y)
constexpr uint32_t kWalkers = 0x00459D7A;          // personnages de la case (EDI x, EBP y)
constexpr uint32_t kObjectsRowEnd = 0x0045A022;    // fin d'une rangée (EDX, ESI, EDI)
// Code dont la réécriture dépend : la boucle et ses tables de sauts (0x0045868C à 0x0045A472),
// FUN_0044f320 jusqu'à sa table, FUN_0045a690 et sa table.
constexpr uint32_t kObjectsCode = 0x0045868C, kObjectsCodeEnd = 0x0045A472;
constexpr uint64_t kObjectsCodeHash = 0xC854D37158F359E7ull;
constexpr uint32_t kBuildingExtras = 0x0044F320, kBuildingExtrasEnd = 0x0044FE78;
constexpr uint64_t kBuildingExtrasHash = 0x89F17B9002B490F8ull;
constexpr uint32_t kSlopedTop = 0x0045A690, kSlopedTopEnd = 0x0045A788;
constexpr uint64_t kSlopedTopHash = 0x5D7835F06E0BE5EFull;
// Petites fonctions réécrites dans la boucle, empreinte commune de leurs octets : FUN_0045daf0,
// FUN_0040aa20, FUN_004200f0, FUN_00423180, FUN_005a7200 et sa table, FUN_004a9590 et sa table,
// FUN_005344a0, FUN_0045dbb0, FUN_0045dc00, FUN_0045dc10, FUN_0045a5a0, débuts de FUN_00449b30
// et de FUN_0041d3d0, FUN_004a9680.
constexpr uint32_t kObjectHelpers[][2] = {
    {0x0045DAF0, 0x0045DB56}, {0x0040AA20, 0x0040AA64}, {0x004200F0, 0x00420113}, {0x00423180, 0x0042319E},
    {0x005A7200, 0x005A72D3}, {0x004A9590, 0x004A967B}, {0x005344A0, 0x005344BC}, {0x0045DBB0, 0x0045DBC6},
    {0x0045DC00, 0x0045DC30}, {0x0045A5A0, 0x0045A63E}, {0x00449B30, 0x00449B48}, {0x0041D3D0, 0x0041D400},
    {0x004A9680, 0x004A96A1}};
constexpr uint64_t kObjectHelpersHash = 0x568E7297F13D31AEull;
constexpr uint32_t kAlwaysFalse = 0x00565750;  // méthode +0x0C du mode de vue 0 : « XOR AL,AL ; RET 8 »
constexpr uint32_t kViewModeZeroSetup = 0x0055E9B0;  // FUN_0055e9b0 : table de méthodes du mode 0

// --- Code : personnages de la ville (native_walkers.cpp) -------------------------------------

constexpr uint32_t kWalker = 0x0044FEF0;      // FUN_0044fef0(personnage, x, y, cherché, plans) : un personnage
constexpr uint32_t kWalkerEnd = 0x00451785;   // fin de FUN_0044fef0 et de ses tables de sauts
constexpr uint64_t kWalkerHash = 0xE9FD6BDE57627C7Dull;
constexpr uint32_t kOnWater = 0x004D7210, kOnWaterEnd = 0x004D7340;  // FUN_004d7210 et ses tables
constexpr uint64_t kOnWaterHash = 0x0D40E7A695A91190ull;
constexpr uint32_t kWaterLife = 0x0041D3D0;   // FUN_0041d3d0(case, x, y, …) : animal ou bateau sur l'eau
// (thiscall) ; empreinte de ses premiers tests dans kWalkerHelpers
// Petites fonctions réécrites dans FUN_0044fef0, empreinte commune : FUN_004200f0, début de
// FUN_0041d3d0, FUN_005344a0, FUN_00411860, FUN_00411870.
constexpr uint32_t kWalkerHelpers[][2] = {
    {0x004200F0, 0x00420113}, {0x0041D3D0, 0x0041D413}, {0x005344A0, 0x005344BC}, {0x00411860, 0x0041186E},
    {0x00411870, 0x00411894}};
constexpr uint64_t kWalkerHelpersHash = 0xA21938BB203C4AA2ull;
constexpr uint32_t kViewFalse = 0x005656F0;   // méthode +0x14 du mode de vue 0 : « XOR AL,AL ; RET 4 »
// FUN_00449b30(case) : trie les personnages d'une case ; empreinte avec son comparateur
// FUN_00449b10, qui la précède, et ses deux tables de sauts.
constexpr uint32_t kWalkerSort = 0x00449B30;
constexpr uint32_t kWalkerSortCode = 0x00449B10, kWalkerSortEnd = 0x00449E44;
constexpr uint64_t kWalkerSortHash = 0xB9E1215F78D8A20Full;

// --- Code : cadencement (user32.cpp) ----------------------------------------------------------

constexpr uint32_t kSimulationDue = 0x005168A0;  // FUN_005168a0 : un tour de simulation est-il dû ?
constexpr uint32_t kSimulationTick = 0x00513A30;  // FUN_00513a30 : un tour de simulation (zeus-run --ticks)

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
// Retours des appels qui dessinent le fond du panneau de contrôle en (x du panneau, 0), image de
// 768 pixels de haut : FUN_00517b10 (panneau déplié) et FUN_00525280 (dernier des deux dessins).
constexpr uint32_t kPanelDrawn = 0x00517B83, kPanelOverlayDrawn = 0x0052531F;
constexpr uint32_t kWorldPanelDrawn = 0x00597ECF;  // FUN_00597e60 : même fond, carte du monde
// FUN_00599da0, carte du monde : fenêtre copiée d'un bloc par FUN_005acf50 (0x005AD6D1). Sa
// largeur est bornée à celle de l'image (0x005FB7AC), pas sa hauteur : hauteur du tampon - 30,
// écrite en 0x00599E20 (MOV [0x00F139D0],EDX ; JL en 0x00599E26).
constexpr uint32_t kWorldWindowRowsSet = 0x00599E20, kWorldWindowRowsChecked = 0x00599E26;
constexpr uint32_t kWindowRows = 0x00F139D0;     // lignes de la fenêtre copiée d'un bloc
constexpr uint32_t kWorldImageHeight = 0x005FB7B0;  // hauteur de l'image de la carte du monde
// FUN_005715a0, fenêtre d'information en mode 1024x768 : ouverte en bas de l'écran (y = 760 - 16 n,
// MOV ECX,0x2F8) si la valeur en 0x00F2A734 (ordonnée du clic, hypothèse) vaut au plus 396
// (CMP [0x00F2A734],0x18C), sinon en haut. Le mode 800x600 suit la même règle : 592 - 16 n et 312.
constexpr uint32_t kInfoWindowBottom = 0x005716A3, kInfoWindowMiddle = 0x00571691;

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

// Vue de la ville (FUN_00458170, FUN_00457900). La carte est une grille de 0x1C9 rangées de
// 0xE7 cases ; les tableaux « par case » sont indexés par le numéro de case lu dans la grille.
constexpr uint32_t kViewRow = 0x007D3BD4, kViewColumn = 0x007D3BD8;  // case en cours de dessin
constexpr uint32_t kViewTile = 0x007D3BE0;     // numéro de cette case
constexpr uint32_t kViewWidth = 0x007D3BBC;    // largeur de la vue en cases (moins 8)
constexpr uint32_t kTileStep = 0x007D3BB0;     // pas horizontal d'une case, moins 2 pixels
// FUN_00449e50 : multiples de la largeur (58) et de la hauteur (30) d'une case.
constexpr uint32_t kHalfTileWidth = 0x007D3BB4, kTileWidth15 = 0x007D3BAC, kTileWidth2 = 0x007D3BA8;  // 29, 87, 116
constexpr uint32_t kTileHeightStep = 0x007D3BA0, kHalfTileHeight = 0x007D3BA4;  // 30, 15
constexpr uint32_t kTileHeight15 = 0x007D3B9C, kTileHeight2 = 0x007D3B98;        // 45, 60
constexpr uint32_t kViewOrientation = 0x00DA2F88;  // orientation de la vue : 0, 2, 4 ou 6
constexpr uint32_t kWalkerRing = 0x0084787C;  // FUN_00458170 : 5 rangées de 0xE7 octets, personnages reportés
constexpr uint32_t kAmbientCounters = 0x010E41F4;  // FUN_00458170 : compteurs par nature, 0x28 octets
// FUN_004200f0(0) : vrai quand ces deux variables valent 1 (partie de Poséidon, hypothèse).
constexpr uint32_t kGameMode = 0x006AA4E0, kGameSubmode = 0x00902BB0;
// FUN_0040aa20 : 6 entrées de 0x34 octets (type de bâtiment en +4), la 6e remplacée pour Poséidon.
constexpr uint32_t kBuildingTable = 0x005E8578, kPoseidonBuildings = 0x005E86B0;
constexpr uint32_t kTileGrid = 0x007D3BEC;     // grille : numéro de case (32 bits), 0x0FFF0000 et plus : marques
constexpr uint32_t kTileHeight = 0x008E01FA;    // par case : hauteur du sol (8 bits signés, 30 pixels par cran)
constexpr uint32_t kTileImage = 0x00D4CFC8;     // par case : image du sol (32 bits)
constexpr uint32_t kTileFlags = 0x00CCE128;     // par case : nature du terrain (32 bits)
constexpr uint32_t kTileBuilding = 0x00D26E98;  // par case : bâtiment (16 bits signés, 0 aucun)
constexpr uint32_t kTileBits = 0x00D404B8;      // par case : 0x40 la case est à dessiner
constexpr uint32_t kTileSlope = 0x00D00D68;     // par case : pente (4 bits bas), 0x10 sol de remplacement
constexpr uint32_t kTileHidden = 0x0083AD6C;    // par case : non nul, ses personnages ne comptent pas
constexpr uint32_t kTileLife = 0x00CB4B08;      // par case : animal ou bateau sur l'eau (8 bits, hypothèse)
constexpr uint32_t kTileFigure = 0x00D0D878;    // par case : premier personnage (16 bits signés)
// Personnages : fiches de 0x184 octets ; +0 suivant sur la case (16 bits), +2 type, +0x5D masqué.
constexpr uint32_t kFigures = 0x00DBD790;
// Champs d'une fiche de personnage lus par FUN_0044fef0, à leur adresse pour la fiche 0.
constexpr uint32_t kFigureImage = 0x00DBD78C;       // image du corps (32 bits)
constexpr uint32_t kFigureType = 0x00DBD792;        // type (8 bits)
constexpr uint32_t kFigureState = 0x00DBD793;       // état (8 bits, FUN_004d7210)
constexpr uint32_t kFigureMoving = 0x00DBD794;      // non nul : position fine (+0x42, +0x44)
// FUN_00449b30 : seul un personnage dont l'octet +6 vaut 1 et l'octet +0x30 n'est pas 4 est
// rangé selon sa position ; les autres passent en tête (sens des deux octets : hypothèse).
constexpr uint32_t kFigurePlaced = 0x00DBD796, kFigureAction = 0x00DBD7C0;
constexpr uint32_t kFigureDirection = 0x00DBD799;   // direction, puis direction de repli (+0x0A)
constexpr uint32_t kFigureAltitude = 0x00DBD7A4;    // retranchée à y en position fine
constexpr uint32_t kFigureLayer = 0x00DBD7A5;       // plan : 0 bit 1 des plans dessinés, sinon bit 2
constexpr uint32_t kFigureTile = 0x00DBD7A8;        // case (32 bits)
constexpr uint32_t kFigureStep = 0x00DBD7C1;        // pas dans la case (8 bits signés)
constexpr uint32_t kFigureDocked = 0x00DBD7C9;      // bateaux : à quai
constexpr uint32_t kFigureFineX = 0x00DBD7D2, kFigureFineY = 0x00DBD7D4;  // position fine (16 bits)
constexpr uint32_t kFigureLink = 0x00DBD7E2;        // personnage ou bâtiment lié (16 bits)
constexpr uint32_t kFigureExtraX = 0x00DBD7F4, kFigureExtraY = 0x00DBD7F5;  // décalage du second sprite
constexpr uint32_t kFigureLift = 0x00DBD800;        // retranchée à y
constexpr uint32_t kFigureDock = 0x00DBD8A4;        // bateaux : case du quai (32 bits)
constexpr uint32_t kFigureThirdImage = 0x00DBD8D8;  // troisième sprite (32 bits)
constexpr uint32_t kFigureColour = 0x00DBD8F0;      // non nul : sprites teints de cette couleur
constexpr uint32_t kFigureKind = 0x00DBD8F4;        // type 6 : sorte d'animal ou de bateau
constexpr uint32_t kFigureFlags = 0x00DBD8F6;       // type 6 : bit 6, sprite retourné
constexpr uint32_t kFigureBuilding = 0x00DBD900;    // type 6 : bâtiment (16 bits)
constexpr uint32_t kFigureExtraImage = 0x00DBD908;  // second sprite (32 bits)
constexpr uint32_t kFigureCart = 0x00DBD7E8;        // type 0x0E : fiche de kCarts (16 bits)
// Type 0x0E : fiches de 0x9C octets ; +0 sorte (16 bits, 11 à 13 : troisième sprite), +0x7E
// (16 bits) positif ou nul : le personnage n'est pas dessiné.
constexpr uint32_t kCarts = 0x00DB7B44;
// Tables de groupes d'images (pointeur vers des groupes de 8 octets, premier identifiant en +4,
// et nombre de groupes) des banques 4 et 0x34 (partie de Poséidon), lues pour le type 0x0E.
constexpr uint32_t kImageGroups4 = 0x012E2CFC, kImageGroups4Count = 0x012EF930;
constexpr uint32_t kImageGroups52 = 0x015479BC, kImageGroups52Count = 0x015545F0;
// FUN_0044fef0 : direction du personnage dans la vue, écrite à chaque appel ; non nul, rien
// n'est dessiné ; position du personnage cherché.
constexpr uint32_t kFigureViewDirection = 0x00FA05E4, kWalkersOff = 0x00B1E8C4;
constexpr uint32_t kFoundFigureY = 0x007D3B38, kFoundFigureX = 0x007D3B3C;
// Décalages par direction et pas (16 par direction), puis par position fine (16 x 16) pour les
// orientations 0 et 4 et pour 2 et 6 ; octets signés.
constexpr uint32_t kStepX = 0x005F0658, kStepY = 0x005F06D8;
constexpr uint32_t kFineX = 0x005F0258, kFineY = 0x005F0358, kFineXTurned = 0x005F0458, kFineYTurned = 0x005F0558;
constexpr uint32_t kTileDepth = 0x00C1C648;    // par case : profondeur de l'eau sous un gué (hypothèse)
constexpr uint32_t kTurnOffsets = 0x005D855C;  // FUN_00411870 : décalage de case par orientation / 2
constexpr uint32_t kWaterNeighbours = 0x005D9188;  // FUN_0044fef0 : sept décalages de case
// FUN_0044fef0 : voisines (rangée, colonne) d'une case de rangée paire ou impaire.
constexpr uint32_t kNeighboursEven = 0x005F8B88, kNeighboursOdd = 0x005F8BD0;
constexpr uint32_t kViewFirstRow = 0x007D3BCC;  // parité des rangées de la vue
constexpr uint32_t kBuildings = 0x0092E668;  // fiches de 0x118 octets ; +0 type (16 bits), +0xBF en rouge
constexpr uint32_t kDrawnImage = 0x00F13A20;   // image de la case en cours de dessin
constexpr uint32_t kViewMode = 0x00FA05F0;     // mode de vue (0 : la ville)
constexpr uint32_t kViewModes = 0x00641C10;    // objet de chaque mode de vue
constexpr uint32_t kHighlight = 0x00665E2C;    // 1 : FUN_0051ef30 peut changer la surimpression
constexpr uint32_t kWaterMoves = 0x007D3B1C;   // non nul : l'eau s'anime
constexpr uint32_t kSpriteBanks = 0x012AEBE8;  // objet des banques d'images (this de FUN_005acf50)
constexpr uint32_t kTerrainGroups = 0x012EF940, kTerrainGroupCount = 0x012FC574;  // groupes d'images
// Images de sol que la boucle ne dessine pas : intervalle et bits, préparés au premier passage.
constexpr uint32_t kSkipReady = 0x00847D28, kSkipFirst = 0x0160C248, kSkipLast = 0x0160C24C,
                   kSkipBits = 0x0160C260;

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
