// Cadres des fenêtres et surbrillances en cases de 16 x 16 pixels (docs/engine/interface.md,
// 5.3 et 12, étape 15), réécrits pour l'hôte :
//
//   - FUN_0059ee90(x, y, colonnes, rangées) : cadre à barre de titre ;
//   - FUN_0059f2a0(x, y, colonnes, rangées) : cadre simple ;
//   - FUN_0059f6e0(a, x, y, largeur, hauteur, variante) : surbrillance d'un rectangle, en pixels ;
//   - FUN_0059ed50(indice, x, y, cases, style) : bouton d'une rangée de cases.
//
// Toutes quatre rangent la position de chaque case dans 0x00F13A40 / 0x00F13A3C (et, sauf la
// surbrillance, son image dans 0x00F13A20), puis la passent au répartiteur FUN_005acf50(0x012AEBE8,
// image, x, y, 0, 0, 0). Les images sont celles de groupes de la banque 1 (« premier » : +4 de
// l'entrée de 8 octets du groupe dans la table 0x012BC830 ; au-delà du nombre de groupes
// 0x012C9464, le groupe 0), plus 0x4000 ; FUN_0040fcf0(groupe) et FUN_0040fd10(0x200 + groupe + 1)
// donnent le même groupe que la lecture directe de la table, faite ailleurs dans les mêmes
// fonctions. Dans un cadre, la case dépend de sa rangée (première, dernière, autres) et de sa
// colonne (première, dernière, autres) ; les cases du milieu d'une rangée avancent d'un pas `s`
// (remis à zéro à chaque rangée et au-delà d'un plafond), celles des côtés d'un pas `p` qui
// avance de toute la hauteur d'un motif à chaque rangée du milieu.
//
// Chaque fonction a un chemin rapide (Cpu::add_fast_path) qui calcule toutes les cases, vérifie
// avant la première écriture que le répartiteur natif les dessine toutes (sprite_handled), puis
// les dessine dans l'ordre de l'original ; sinon il rend la main sans avoir rien écrit et
// l'original fait tout. Le répartiteur ne lit ni les variables qu'écrivent ces fonctions ni la
// pile de l'appelant, et ce que lit sprite_handled (tables d'images, rectangle de dessin,
// surimpression, écran) n'est écrit par aucun dessin : la décision prise pour toutes les cases
// d'abord est celle que prendrait chaque appel. EAX est celui que laisse l'original ; les
// emplacements d'arguments que l'original réutilise reçoivent les mêmes valeurs.
//
// Adresses : win32/game_gog.h. Le code n'est modifié que si ses octets sont ceux attendus
// (empreinte FNV-1a, comparée avant toute modification).

#include "win32/native_frames.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
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
void put32(Cpu& cpu, uint32_t address, uint32_t value) { std::memcpy(cpu.host(address), &value, 4); }

// Premier numéro d'image du groupe `group` de la banque 1, plus 0x4000 (16384 × banque) :
// « CMP [0x012C9464],groupe ; JL » vers le groupe 0, comparaison signée.
uint32_t group_first(Cpu& cpu, int32_t group) {
    const uint32_t table = u32(cpu, kFrameGroups);
    const uint32_t entry = i32(cpu, kFrameGroupCount) >= group ? table + static_cast<uint32_t>(group) * 8 : table;
    return u32(cpu, entry + 4) + 0x4000;
}

// Une case : image, position écrite dans kTargetX / kTargetY, position passée au répartiteur
// (décalée dans les cases du bord droit et du bas d'une surbrillance), et si l'original la
// dessine et écrit son image dans kDrawnImage.
struct Tile {
    uint32_t id, target_x, target_y, x, y;
    bool drawn, image;
};

// Tampon réutilisé d'un appel à l'autre (les chemins rapides s'exécutent sur le fil de l'émulateur).
std::vector<Tile>& tiles() {
    static std::vector<Tile> list;
    list.clear();
    return list;
}

// Décide, puis dessine : faux sans avoir rien écrit quand une case n'est pas un cas du
// répartiteur natif. `last` reçoit la valeur rendue par le dernier dessin.
bool draw_tiles(Cpu& cpu, const std::vector<Tile>& list, uint32_t* last = nullptr) {
    for (const Tile& t : list)
        if (t.drawn && !sprite_handled(cpu, kSpriteBanks, static_cast<int32_t>(t.id), static_cast<int32_t>(t.x),
                                       static_cast<int32_t>(t.y), 0, 0))
            return false;
    for (const Tile& t : list) {
        put32(cpu, kTargetX, t.target_x);
        put32(cpu, kTargetY, t.target_y);
        if (t.image) put32(cpu, kDrawnImage, t.id);
        if (!t.drawn) continue;
        const std::optional<uint32_t> r = draw_sprite(cpu, kSpriteBanks, static_cast<int32_t>(t.id),
                                                      static_cast<int32_t>(t.x), static_cast<int32_t>(t.y), 0, 0);
        if (last) *last = r.value_or(0);  // toujours traité : sprite_handled l'a dit, rien de ce qu'il lit n'a changé
    }
    return true;
}

// --- FUN_0059ee90 et FUN_0059f2a0(x, y, colonnes, rangées) : cadres ---------------------------

// Décalages des cases depuis le premier du groupe (0x4000 compris) : rangée du haut, du bas, et
// rangées du milieu (« côtés ») ; pour chacune, première colonne, dernière, autres (+ s). Les
// côtés ajoutent p à leurs trois cases.
struct FrameKind {
    int32_t group;
    uint32_t top[3], bottom[3], side[3];
    uint32_t side_step;  // EDI des cases des côtés, ajouté à p à la fin de la rangée
    int32_t s_wrap, p_wrap;
};

// FUN_0059ee90 : groupe 0x83 (lu directement, par FUN_0040fcf0(0x83) ou FUN_0040fd10(0x284)) ;
// s remis à zéro à 10 (0x0059F0AA), p à 0x78 (0x0059F0C4).
constexpr FrameKind kTitled = {0x83, {0x00, 0x0B, 0x01}, {0x84, 0x8F, 0x85}, {0x0C, 0x17, 0x0D}, 0x0C, 10, 0x78};
// FUN_0059f2a0 : groupe 0x84 (FUN_0040fcf0(0x84), FUN_0040fd10(0x285)) ; s à 5, p à 0x23.
constexpr FrameKind kPlain = {0x84, {0x00, 0x06, 0x01}, {0x2A, 0x30, 0x2B}, {0x07, 0x0D, 0x08}, 0x07, 5, 0x23};

// Rangée 0 d'abord, puis la dernière ; dans une rangée, colonne 0 d'abord, puis la dernière
// (comparaisons signées avec colonnes - 1 et rangées - 1).
std::optional<uint32_t> frame(Cpu& cpu, const FrameKind& k) {
    const uint32_t x0 = cpu.arg(0), y0 = cpu.arg(1);
    const int32_t columns = static_cast<int32_t>(cpu.arg(2)), rows = static_cast<int32_t>(cpu.arg(3));
    if (rows <= 0) return 0u;  // 0x0059EEA5 : EAX nul, rien d'écrit
    std::vector<Tile>& list = tiles();
    const uint32_t first = group_first(cpu, k.group);
    uint32_t p = 0;  // EBP
    for (int32_t r = 0; r < rows; ++r) {
        uint32_t s = 0, step = 0;  // ESI, EDI
        const bool side = r != 0 && r < rows - 1;
        const uint32_t* offsets = r == 0 ? k.top : side ? k.side : k.bottom;
        const uint32_t y = y0 + static_cast<uint32_t>(r) * 16;
        for (int32_t c = 0; c < columns; ++c) {
            uint32_t id = first;
            if (c == 0) {
                id += offsets[0];
            } else if (c >= columns - 1) {
                id += offsets[1];
            } else {
                id += offsets[2] + s;
                ++s;
            }
            if (side) id += p;
            step = side ? k.side_step : 0;
            const uint32_t x = x0 + static_cast<uint32_t>(c) * 16;
            list.push_back({id, x, y, x, y, true, true});
            if (static_cast<int32_t>(s) >= k.s_wrap) s = 0;
        }
        p += step;
        if (static_cast<int32_t>(p) >= k.p_wrap) p = 0;
    }
    if (!draw_tiles(cpu, list)) return std::nullopt;
    return static_cast<uint32_t>(rows);  // compteur de rangées en sortie de boucle (0x0059F0CF)
}

std::optional<uint32_t> titled_frame_call(Cpu& cpu) { return frame(cpu, kTitled); }
std::optional<uint32_t> plain_frame_call(Cpu& cpu) { return frame(cpu, kPlain); }

// --- FUN_0059f6e0(a, x, y, largeur, hauteur, variante) : surbrillance --------------------------

// Colonnes et rangées : division entière signée par 16, plus une s'il reste quelque chose
// (0x0059F6EA à 0x0059F736). Les cases de la dernière colonne sont dessinées décalées vers la
// gauche, celles de la dernière rangée vers le haut, de ce qui dépasse ; kTargetX / kTargetY
// reçoivent la position non décalée. Le premier argument n'est pas lu ; une variante non nulle
// prend les huit images suivantes du groupe 0x7B. Les cases intérieures ne sont pas dessinées
// (0x0059F885), seule leur position est écrite ; kDrawnImage ne l'est jamais.
std::optional<uint32_t> highlight_call(Cpu& cpu) {
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    const uint32_t x0 = cpu.arg(1), y0 = cpu.arg(2);
    const int32_t width = static_cast<int32_t>(cpu.arg(3)), height = static_cast<int32_t>(cpu.arg(4));
    const int32_t columns = width / 16 + (width % 16 != 0 ? 1 : 0);
    const int32_t rows = height / 16 + (height % 16 != 0 ? 1 : 0);
    const uint32_t dx = static_cast<uint32_t>(columns) * 16 - static_cast<uint32_t>(width);  // EBX
    const uint32_t dy = static_cast<uint32_t>(rows) * 16 - static_cast<uint32_t>(height);    // EBP
    const uint32_t base = group_first(cpu, 0x7B) + (cpu.arg(5) != 0 ? 8 : 0);               // ESI
    std::vector<Tile>& list = tiles();
    for (int32_t r = 0; r < rows; ++r) {
        const uint32_t y = y0 + static_cast<uint32_t>(r) * 16;
        for (int32_t c = 0; c < columns; ++c) {
            const uint32_t x = x0 + static_cast<uint32_t>(c) * 16;
            const bool left = c == 0, right = !left && c >= columns - 1;
            Tile t{0, x, y, right ? x - dx : x, y, true, false};
            if (r == 0) {
                t.id = base + (left ? 0 : right ? 2 : 1);
            } else if (r >= rows - 1) {
                t.y = y - dy;
                t.id = base + (left ? 6 : right ? 4 : 5);
            } else {
                t.id = base + (left ? 7 : 3);
                t.drawn = left || right;
            }
            list.push_back(t);
        }
    }
    if (!draw_tiles(cpu, list)) return std::nullopt;
    // Emplacements d'arguments réutilisés : y de la rangée (0x0059F7E0, écrit seulement quand il y
    // a des colonnes), colonnes (0x0059F6FC), rangées (0x0059F724), compteur de rangées (0x0059F7BF).
    if (rows > 0 && columns > 0) put32(cpu, esp + 4, y0 + static_cast<uint32_t>(rows - 1) * 16);
    put32(cpu, esp + 0x10, static_cast<uint32_t>(columns));
    put32(cpu, esp + 0x14, static_cast<uint32_t>(rows));
    const uint32_t counter = rows > 0 ? static_cast<uint32_t>(rows) : 0;
    put32(cpu, esp + 0x18, counter);
    return counter;  // EAX : le compteur de rangées
}

// --- FUN_0059ed50(indice, x, y, cases, style) : bouton -------------------------------------------

// Une rangée de cases du groupe 0x0E, trois images par style (gauche, milieu, droite). La longueur
// vient de la table 0x010DFEE0 (32 bits par indice ; tous les appelants passent 6, la case
// 0x010DFEF8, mise à 100 en 0x00424E2C et 0x005900E9) : rien en dessous de 15 (EAX = la valeur) ;
// sinon valeur - 13 cases, ramenées au nombre demandé, ou centrées dans la place demandée
// (x + (cases - longueur) × 16 / 2, rangé dans l'emplacement de x). EAX : la valeur du dernier
// appel du répartiteur, ou le nombre de cases demandé quand il est nul ou négatif.
std::optional<uint32_t> button_call(Cpu& cpu) {
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    const int32_t value = i32(cpu, kButtonLengths + cpu.arg(0) * 4);
    if (value < 15) return static_cast<uint32_t>(value);  // 0x0059ED60, 0x0059ED69
    const int32_t wanted = static_cast<int32_t>(cpu.arg(3));
    int32_t length = value - 13;
    uint32_t x0 = cpu.arg(1);
    bool moved = false;
    if (length > wanted) {
        length = wanted;
        if (length <= 0) return static_cast<uint32_t>(wanted);  // 0x0059ED96
    } else {
        // « SHL EAX,4 ; CDQ ; SUB EAX,EDX ; SAR EAX,1 » : division signée par 2, vers zéro.
        const int32_t room = static_cast<int32_t>(static_cast<uint32_t>(wanted - length) << 4);
        x0 += static_cast<uint32_t>(room / 2);
        moved = true;
    }
    const uint32_t y = cpu.arg(2);
    const uint32_t base = group_first(cpu, 0x0E) + cpu.arg(4) * 3;
    std::vector<Tile>& list = tiles();
    for (int32_t i = 0; i < length; ++i) {
        const uint32_t id = base + (i == 0 ? 0 : i >= length - 1 ? 2 : 1);
        const uint32_t x = x0 + static_cast<uint32_t>(i) * 16;
        list.push_back({id, x, y, x, y, true, true});
    }
    uint32_t last = 0;
    if (!draw_tiles(cpu, list, &last)) return std::nullopt;
    if (moved) put32(cpu, esp + 8, x0);  // 0x0059ED8E
    return last;
}

// Empreinte FNV-1a 64 bits de zones de la mémoire du jeu mises bout à bout.
uint64_t fingerprint(Cpu& cpu, const uint32_t (*ranges)[2], size_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < count; ++i)
        for (uint32_t a = ranges[i][0]; a < ranges[i][1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_frames(Process& p) {
    Cpu& cpu = p.cpu();
    // Code réécrit : les quatre fonctions, FUN_0040fcf0 et FUN_0040fd10 (une seule empreinte).
    if (fingerprint(cpu, kFrameCode, std::size(kFrameCode)) != kFrameCodeHash) return 0;
    cpu.add_fast_path(kButtonDraw, 0, 5, button_call);             // « MOV EAX,[ESP+4] ; PUSH EBX »
    cpu.add_fast_path(kTitledFrameDraw, 0, 7, titled_frame_call);  // « SUB ESP,8 ; MOV ECX,[ESP+0x18] »
    cpu.add_fast_path(kFrameDraw, 0, 7, plain_frame_call);         // « SUB ESP,8 ; MOV ECX,[ESP+0x18] »
    cpu.add_fast_path(kHighlightDraw, 0, 7, highlight_call);       // « PUSH EBX ; PUSH EBP ; PUSH ESI ; MOV ESI,[ESP+0x1C] »
    return 4;
}

}  // namespace zeus::win32
