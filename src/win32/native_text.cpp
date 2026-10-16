// Textes réécrits pour l'hôte : la recherche d'une chaîne et la mise en page des textes des
// panneaux (docs/engine/interface.md §7.1, §7.3 et §7.5).
//
//   - FUN_00507bf0(groupe, rang) : adresse d'une chaîne du fichier de textes. Remplacée entière :
//     elle parcourt toutes les chaînes du groupe qui précèdent la chaîne voulue, si bien qu'un
//     appel coûte des centaines de blocs émulés.
//   - FUN_004f5300, la mise en page : deux aiguillages (Cpu::add_code_jump) dans sa boucle de
//     lignes. Le premier efface le tampon de la ligne (un REP STOSD que l'émulateur exécute en un
//     bloc par mot) ; le second remplit la ligne mot par mot, avec la mesure de chaque mot
//     (FUN_004f57a0, et FUN_00507f20 pour chaque caractère) faite ici. L'image en ligne (@G)
//     et les lignes qu'elle réserve restent à l'original, qui revient ensuite au second
//     aiguillage. Le dessin de la ligne (FUN_004f58c0) reste à l'original.
//
// FUN_004f57a0 et FUN_00507f20 ne sont pas interceptées : leurs 2,2 et 10 millions d'appels
// (working-city) coûteraient chacun l'entrée d'une fonction native (environ 140 ns), alors que
// l'aiguillage du remplissage n'est atteint qu'une fois par ligne. Leurs autres appelants
// (FUN_004f58c0 pour un lien, FUN_005091b0) gardent l'original.
//
// Le code des quatre fonctions doit avoir l'empreinte attendue (FNV-1a sur toute la fonction),
// sinon rien n'est installé : le pack de langue ne modifie aucune d'elles (il change la table
// des glyphes, qui est lue ici à chaque caractère, et FUN_004f59b0).

#include <cstdint>
#include <cstring>

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

uint64_t fingerprint(Cpu& cpu, uint32_t start, uint32_t end) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t a = start; a < end; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

// FUN_00507bf0(groupe, rang) : depuis le début du groupe, chaque chaîne à sauter se termine à un
// zéro qui suit un caractère imprimable ou un autre zéro ; les octets de contrôle qui suivent sont
// ensuite sautés.
uint32_t find_string(Cpu& cpu) {
    const uint32_t group = cpu.arg(0);
    int32_t rank = static_cast<int32_t>(cpu.arg(1));
    uint32_t p = u32(cpu, kTextGroups + 8 * group) + u32(cpu, kTextBase);
    const uint8_t* m = cpu.host(0);
    while (rank > 0) {
        if (m[p] == 0) {
            const uint8_t before = m[p - 1];
            if (before >= 0x20 || before == 0) --rank;
        }
        ++p;
    }
    if (m[p] < 0x20)
        do ++p;
        while (m[p] < 0x20);
    return p;
}

// FUN_00507f20(caractère, police) pour un caractère imprimable autre que l'espace : le numéro du
// glyphe (laissé dans kDrawnImage, comme l'original), puis la largeur + 1 de l'image
// police + glyphe - 1 ; un numéro hors de la banque lit l'image 0.
int32_t char_width(Cpu& cpu, uint8_t ch, uint32_t font) {
    const uint32_t glyph = *cpu.host(kGlyphTable + ch);
    put32(cpu, kDrawnImage, glyph);
    if (glyph == 0) return 0;
    const int32_t id = static_cast<int32_t>(glyph + font - 1);
    const uint32_t bank = kSpriteBanks + 0x1000 + static_cast<uint32_t>(id / 16384) * 0xCC44;
    const int32_t number = id % 16384;
    const uint32_t images = u32(cpu, bank);
    const uint32_t entry = number <= i32(cpu, bank + 0xCC34) ? images + static_cast<uint32_t>(number) * 72 : images;
    return s16(cpu, entry + 0x14) + 1;
}

// FUN_004f57a0(texte) : largeur du mot suivant ; kWordBytes reçoit le nombre d'octets pris (un
// « @ » dans un mot n'est pas compté). Avant le mot : une espace vaut 4, @P et @L terminent
// (largeur 0, 2 octets), @G et ses chiffres aussi (largeur 0), @chiffres saute les chiffres.
// L'original range aussi le dernier caractère lu dans l'octet bas de son argument : cet
// emplacement est sous la pile de FUN_004f5300 au retour, il n'est pas écrit ici.
int32_t word_width(Cpu& cpu, uint32_t text) {
    const uint8_t* m = cpu.host(0);
    const uint32_t font = u32(cpu, kPanelFont);
    uint32_t p = text, count = 0;
    int32_t width = 0;
    bool in_word = false;
    for (int n = 1; n < 2000; ++n) {
        uint8_t ch = m[p++];
        if (ch == 0) break;
        if (ch == '@') {
            ch = m[p++];
            if (!in_word) {
                if (ch == 'P' || ch == 'L') {
                    put32(cpu, kWordBytes, count + 2);
                    return 0;
                }
                if (ch == 'G') {
                    count += 2;
                    ch = m[p++];
                    while (ch >= '0' && ch <= '9') {
                        ch = m[p++];
                        ++count;
                    }
                    put32(cpu, kWordBytes, count);
                    return 0;
                }
                ++count;
                if (ch >= '0' && ch <= '9')
                    do {
                        ch = m[p++];
                        ++count;
                    } while (ch >= '0' && ch <= '9');
            }
        }
        if (ch == ' ') {
            if (in_word) break;
            width += 4;
        } else if (ch < ' ') {
            if (in_word) break;
        } else {
            width += char_width(cpu, ch, font);
            in_word = true;
        }
        ++count;
    }
    put32(cpu, kWordBytes, count);
    return width;
}

// Variables locales de FUN_004f5300, vues depuis ESP dans sa boucle de lignes.
constexpr uint32_t kImageLines = 0x10;  // lignes encore réservées par une image @G
constexpr uint32_t kIndentNext = 0x1C;  // @L vu : la ligne suivante commence sans retrait
constexpr uint32_t kMore = 0x20;        // il reste du texte
constexpr uint32_t kParagraph = 0x28;   // @P vu
constexpr uint32_t kLength = 0x2C;      // octets dans le tampon de la ligne
constexpr uint32_t kLineStart = 0x34;   // rien encore dans la ligne : les espaces sont sautées
constexpr uint32_t kWidth = 0x48;       // argument : largeur disponible

// 0x004F536C : efface les 200 octets du tampon de la ligne, registres comme après le REP STOSD.
uint32_t clear_line(Cpu& cpu) {
    std::memset(cpu.host(kLineBuffer), 0, 200);
    cpu.set_reg(Cpu::Reg::EAX, 0);
    cpu.set_reg(Cpu::Reg::ECX, 0);
    cpu.set_reg(Cpu::Reg::EBX, 0x32);
    cpu.set_reg(Cpu::Reg::EDI, kLineBuffer + 200);
    return kPanelLineClearEnd;
}

// 0x004F53B6, EDX nul : ajoute les mots qui tiennent dans la ligne (EBX : largeur prise, EBP :
// texte) et poursuit en 0x004F554C quand la ligne est faite, ou rend la main à l'original pour
// une ligne réservée par une image (0x004F5548) ou pour le code @G (0x004F5455).
uint32_t fill_line(Cpu& cpu) {
    const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
    const uint8_t* m = cpu.host(0);
    const int32_t limit = i32(cpu, esp + kWidth);
    int32_t width = static_cast<int32_t>(cpu.reg(Cpu::Reg::EBX));
    uint32_t text = cpu.reg(Cpu::Reg::EBP);
    uint32_t esi = static_cast<uint32_t>(limit);
    uint32_t next = kPanelLineDone;
    for (;;) {
        // 0x004F53B6
        esi = static_cast<uint32_t>(limit);
        if (width >= limit) break;
        if (u32(cpu, esp + kImageLines) != 0) {
            next = kPanelImageLine;
            break;
        }
        const int32_t w = word_width(cpu, text);
        width += w;
        if (width >= limit) {
            if (width == 0) put32(cpu, esp + kMore, 0);
        } else {
            const int32_t count = i32(cpu, kWordBytes);
            esi = static_cast<uint32_t>(count);
            uint32_t length = u32(cpu, esp + kLength);
            bool image = false;
            for (int32_t i = 0; i < count; ++i) {
                const uint8_t ch = m[text++];
                if (ch == '@') {
                    const uint8_t code = m[text];
                    if (code == 'P' || code == 'L') {
                        width = limit;
                        put32(cpu, esp + (code == 'P' ? kParagraph : kIndentNext), 1);
                        ++text;
                        break;
                    }
                    if (code == 'G') {
                        cpu.set_reg(Cpu::Reg::EAX, (static_cast<uint32_t>(w) & 0xFFFFFF00u) | code);
                        cpu.set_reg(Cpu::Reg::ECX, (cpu.reg(Cpu::Reg::ECX) & 0xFFFFFF00u) | ch);
                        cpu.set_reg(Cpu::Reg::EDX, static_cast<uint32_t>(i));
                        cpu.set_reg(Cpu::Reg::EDI, 0);
                        image = true;
                        break;
                    }
                }
                if (u32(cpu, esp + kLineStart) != 0 && ch == ' ') continue;
                put32(cpu, esp + kLineStart, 0);
                *cpu.host(kLineBuffer + length) = ch;
                put32(cpu, esp + kLength, ++length);
            }
            if (image) {
                cpu.set_reg(Cpu::Reg::EBX, static_cast<uint32_t>(width));
                cpu.set_reg(Cpu::Reg::EBP, text);
                cpu.set_reg(Cpu::Reg::ESI, esi);
                return kPanelImageCode;
            }
            // 0x004F552D
            if (m[text] == 0) put32(cpu, esp + kMore, 0);
        }
        // 0x004F53A4
        if (u32(cpu, esp + kMore) == 0 && u32(cpu, esp + kImageLines) == 0) break;
    }
    cpu.set_reg(Cpu::Reg::EBX, static_cast<uint32_t>(width));
    cpu.set_reg(Cpu::Reg::EBP, text);
    cpu.set_reg(Cpu::Reg::ESI, esi);
    cpu.set_reg(Cpu::Reg::EDI, 0);
    return next;
}

struct Fingerprint {
    uint32_t address, end;
    uint64_t hash;
};

constexpr Fingerprint kFingerprints[] = {
    {kFindString, kFindStringEnd, 0xF2D9C2F55EAA4DDBull},
    {kPanelText, kPanelTextEnd, 0xA6136A6B07FDB439ull},
    {kWordWidth, kWordWidthEnd, 0x3311B9064EF4F729ull},
    {kCharWidth, kCharWidthEnd, 0x11FCAAECD6E5A062ull},
};

}  // namespace

int install_native_text(Process& p) {
    Cpu& cpu = p.cpu();
    for (const Fingerprint& f : kFingerprints)
        if (fingerprint(cpu, f.address, f.end) != f.hash) return 0;
    cpu.replace_function(kFindString, 0, find_string);
    cpu.add_code_jump(kPanelLineClear, 7, clear_line);  // « MOV EBX,0x32 ; XOR EAX,EAX » déplacés
    cpu.add_code_jump(kPanelLineFill, 6, fill_line);    // « MOV ESI,[ESP+0x48] ; CMP EBX,ESI » déplacés
    return 3;
}

}  // namespace zeus::win32
