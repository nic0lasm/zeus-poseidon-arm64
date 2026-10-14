// Tests des pièces de l'hôte qui ne dépendent pas du jeu : aucun fichier du jeu n'est lu.
//
//   zeus-tests [DOSSIER]   DOSSIER : dossier temporaire des essais du Vfs (défaut : celui de la
//                          construction, build/tests-tmp) ; il est vidé au début.
//
// Chaque vérification qui échoue est affichée avec sa ligne ; le code de sortie est 1 s'il y en a.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "win32/audio.h"
#include "win32/cpu.h"
#include "win32/desktop.h"
#include "win32/sprite_clip.h"
#include "win32/text.h"
#include "win32/vfs.h"

#ifndef ZEUS_TEST_DIR
#define ZEUS_TEST_DIR "build/tests-tmp"
#endif

namespace {

namespace fs = std::filesystem;
using namespace zeus::win32;

int checks = 0, failures = 0;

void check(bool ok, const char* what, int line) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("ÉCHEC ligne %d : %s\n", line, what);
}
#define CHECK(x) check((x), #x, __LINE__)

bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

bool near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

// --- lecteur WAV --------------------------------------------------------------------------------

void put16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    put16(v, x & 0xFFFF);
    put16(v, x >> 16);
}

void put_tag(std::vector<uint8_t>& v, const char* tag) { v.insert(v.end(), tag, tag + 4); }

// Fichier WAV construit en mémoire ; `extra` : bloc inconnu placé avant les données.
std::vector<uint8_t> make_wav(int format, int channels, int rate, int bits, const std::vector<uint8_t>& data,
                              bool extra = false) {
    std::vector<uint8_t> body;
    put_tag(body, "WAVE");
    put_tag(body, "fmt ");
    put32(body, 16);
    put16(body, static_cast<uint32_t>(format));
    put16(body, static_cast<uint32_t>(channels));
    put32(body, static_cast<uint32_t>(rate));
    put32(body, static_cast<uint32_t>(rate * channels * bits / 8));
    put16(body, static_cast<uint32_t>(channels * bits / 8));
    put16(body, static_cast<uint32_t>(bits));
    if (extra) {  // longueur impaire : suivie d'un octet de bourrage
        put_tag(body, "LIST");
        put32(body, 3);
        body.insert(body.end(), {'a', 'b', 'c', 0});
    }
    put_tag(body, "data");
    put32(body, static_cast<uint32_t>(data.size()));
    body.insert(body.end(), data.begin(), data.end());
    std::vector<uint8_t> file;
    put_tag(file, "RIFF");
    put32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

void test_wav() {
    // 16 bits stéréo : deux trames.
    const auto stereo = make_wav(1, 2, 22050, 16, {0x01, 0x00, 0xFF, 0xFF, 0x00, 0x80, 0xFF, 0x7F});
    CHECK(wav_size(stereo.data(), stereo.size()) == stereo.size());
    const auto s = decode_wav(stereo.data(), stereo.size());
    CHECK(s && s->rate == 22050 && s->channels == 2 && s->frames() == 2 && s->ready);
    CHECK(s && s->samples == (std::vector<int16_t>{1, -1, -32768, 32767}));

    // 8 bits mono, avec un bloc inconnu de longueur impaire avant les données.
    const auto mono = make_wav(1, 1, 11025, 8, {0x80, 0x00, 0xFF}, true);
    const auto m = decode_wav(mono.data(), mono.size());
    CHECK(m && m->rate == 11025 && m->channels == 1 && m->frames() == 3);
    CHECK(m && m->samples == (std::vector<int16_t>{0, -32768, 0x7F00}));

    // 16 bits stéréo avec une trame incomplète : elle est retirée.
    const auto odd = make_wav(1, 2, 8000, 16, {1, 0, 2, 0, 3, 0});
    const auto o = decode_wav(odd.data(), odd.size());
    CHECK(o && o->frames() == 1 && o->samples.size() == 2);

    // Formats refusés : ADPCM, 24 bits, 3 voies, en-tête absent ou tronqué.
    const auto adpcm = make_wav(2, 1, 22050, 16, {0, 0});
    CHECK(decode_wav(adpcm.data(), adpcm.size()) == nullptr);
    const auto deep = make_wav(1, 1, 22050, 24, {0, 0, 0});
    CHECK(decode_wav(deep.data(), deep.size()) == nullptr);
    const auto three = make_wav(1, 3, 22050, 16, {0, 0, 0, 0, 0, 0});
    CHECK(decode_wav(three.data(), three.size()) == nullptr);
    const uint8_t junk[] = "RIFX....WAVE";
    CHECK(wav_size(junk, 12) == 0 && decode_wav(junk, 12) == nullptr);
    CHECK(wav_size(stereo.data(), 11) == 0);
    CHECK(decode_wav(stereo.data(), stereo.size() - 4) == nullptr);  // données plus courtes qu'annoncé
}

// --- script d'essai ------------------------------------------------------------------------------

void test_script() {
    using Kind = ScriptEvent::Kind;
    const auto e = parse_script("5:click 400,300; 1.5:key 27; 9:char 65 ;  ; 10:shot capture.bmp");
    CHECK(e.size() == 7);
    if (e.size() == 7) {
        CHECK(e[0].kind == Kind::Move && e[0].at == 5 && e[0].x == 400 && e[0].y == 300);
        CHECK(e[1].kind == Kind::LeftDown && near(e[1].at, 5.15) && e[1].x == 400);
        CHECK(e[2].kind == Kind::LeftUp && near(e[2].at, 5.3));
        CHECK(e[3].kind == Kind::KeyDown && e[3].at == 1.5 && e[3].key == 27);
        CHECK(e[4].kind == Kind::KeyUp && near(e[4].at, 1.65));
        CHECK(e[5].kind == Kind::Char && e[5].key == 65);
        CHECK(e[6].kind == Kind::Shot && e[6].path == "capture.bmp");
    }
    // Glissement : la souris arrive, le bouton suit une demi-seconde plus tard.
    const auto drag = parse_script("2:down 10,20; 3:move 30,20; 4:up 50,20");
    CHECK(drag.size() == 5);
    if (drag.size() == 5) {
        CHECK(drag[1].kind == Kind::LeftDown && near(drag[1].at, 2.5));
        CHECK(drag[2].kind == Kind::Move && drag[2].x == 30);
        CHECK(drag[4].kind == Kind::LeftUp && near(drag[4].at, 4.5) && drag[4].x == 50);
    }
    const auto other = parse_script("1:rclick 5,6; 2:wheel -3; 3:blur; 4:focus");
    CHECK(other.size() == 6);
    if (other.size() == 6) {
        CHECK(other[1].kind == Kind::RightDown && other[2].kind == Kind::RightUp);
        CHECK(other[3].kind == Kind::Wheel && other[3].key == -3);
        CHECK(other[4].kind == Kind::Blur && other[5].kind == Kind::Focus);
    }
    CHECK(parse_script("").empty());
    CHECK(parse_script(" ; ").empty());
    CHECK(throws([] { parse_script("click 1,2"); }));       // sans instant
    CHECK(throws([] { parse_script("1:jump 1,2"); }));      // action inconnue
    CHECK(throws([] { parse_script("1:click 400"); }));     // position incomplète
    CHECK(throws([] { parse_script("x:click 1,2"); }));     // instant illisible
}

// --- Windows-1252 et UTF-8 ----------------------------------------------------------------------

void test_text() {
    CHECK(to_utf8("Zeus") == "Zeus");
    CHECK(to_utf8("Pos\xE9idon") == "Pos\xC3\xA9idon");                // é
    CHECK(to_utf8("\x80") == "\xE2\x82\xAC");                          // €
    CHECK(to_utf8("\x9C\x8C") == "\xC5\x93\xC5\x92");                  // œŒ
    CHECK(to_utf8("\x81") == "\xC2\x81");                              // octet non attribué : gardé
    CHECK(from_utf8("Pos\xC3\xA9idon") == "Pos\xE9idon");
    CHECK(from_utf8("\xE2\x82\xAC 5") == "\x80 5");
    CHECK(from_utf8("\xE2\x80\x99") == "\x92");                        // ’
    CHECK(from_utf8("\xE6\x97\xA5") == "?");                           // 日 : absent de Windows-1252
    CHECK(from_utf8("\xF0\x9F\x98\x80!") == "?!");                     // hors du plan de base
    // Aller-retour de chaque octet attribué.
    for (int b = 1; b < 256; ++b) {
        if (b == 0x81 || b == 0x8D || b == 0x8F || b == 0x90 || b == 0x9D) continue;
        const std::string one(1, static_cast<char>(b));
        if (from_utf8(to_utf8(one)) != one) {
            check(false, "aller-retour Windows-1252", __LINE__);
            std::printf("  octet %#x\n", b);
        }
    }
}

// --- Vfs : recherche insensible à la casse ------------------------------------------------------

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void test_vfs(const fs::path& root) {
    fs::remove_all(root);
    const fs::path game = root / "jeu", saves = root / "ecritures";
    write_file(game / "Data" / "Zeus_Text.ENG", "texte");
    write_file(game / "Adventures" / "Pos\xC3\xA9idon.mp", "aventure");  // nom UTF-8 sur l'hôte
    write_file(game / "Zeus.ini", "origine");
    Vfs vfs(game, saves, "C:\\Zeus");

    const auto text = vfs.find("C:\\ZEUS\\data\\ZEUS_TEXT.eng");
    CHECK(text && text->filename() == "Zeus_Text.ENG");
    CHECK(vfs.find("data/zeus_text.eng").has_value());                        // relatif, barres obliques
    CHECK(vfs.find("C:\\Zeus\\ADVENTURES\\pos\xE9idon.MP").has_value());       // nom du jeu en Windows-1252
    CHECK(!vfs.find("C:\\Zeus\\Data\\absent.txt"));
    CHECK(!vfs.find("D:\\Autre\\Zeus.ini"));                                  // hors de l'installation
    CHECK(vfs.absolute("data\\..\\Zeus.ini") == "C:\\Zeus\\Zeus.ini");

    // Écriture : jamais dans l'installation ; le fichier écrit masque celui de l'installation.
    const auto target = vfs.writable("C:\\Zeus\\zeus.INI", true);
    CHECK(target && target->string().rfind(saves.string(), 0) == 0);
    CHECK(target && read_file(*target) == "origine");
    if (target) write_file(*target, "modifié");
    const auto ini = vfs.find("ZEUS.ini");
    CHECK(ini && read_file(*ini) == "modifié");
    CHECK(read_file(game / "Zeus.ini") == "origine");
    CHECK(!vfs.writable("D:\\Autre\\x.txt", false));

    // Liste avec jokers, sans doublon entre installation et dossier d'écriture.
    CHECK(vfs.list("C:\\Zeus\\*.ini").size() == 1);
    CHECK(vfs.list("C:\\Zeus\\DATA\\zeus_*.e?g").size() == 1);
    CHECK(vfs.list("C:\\Zeus\\Data\\*.txt").empty());

    // Suppression : le fichier de l'installation disparaît pour le programme, pas de l'hôte.
    CHECK(vfs.remove("C:\\Zeus\\Data\\Zeus_Text.eng"));
    CHECK(!vfs.find("C:\\Zeus\\Data\\Zeus_Text.eng"));
    CHECK(fs::exists(game / "Data" / "Zeus_Text.ENG"));
    CHECK(vfs.make_directory("C:\\Zeus\\Save\\Nouveau") && vfs.find("c:\\zeus\\save\\NOUVEAU").has_value());
}

// --- rognage du répartiteur de sprites ----------------------------------------------------------

void test_clip() {
    // Rectangle de dessin 0..1024 en largeur, 40..700 en hauteur.
    auto h = clip_horizontal(100, 58, 0, 1024);  // entière
    CHECK(h.state == 0 && h.left == 100 && h.skip_left == 0 && h.right == 58 && h.visible == 58);
    h = clip_horizontal(-20, 58, 0, 1024);  // rognée à gauche
    CHECK(h.state == 1 && h.left == 0 && h.skip_left == 20 && h.right == 58 && h.visible == 38);
    h = clip_horizontal(1000, 58, 0, 1024);  // rognée à droite
    CHECK(h.state == 2 && h.left == 1000 && h.skip_left == 0 && h.right == 24 && h.visible == 24);
    h = clip_horizontal(966, 58, 0, 1024);  // touche le bord droit sans le dépasser
    CHECK(h.state == 0 && h.visible == 58);
    h = clip_horizontal(-58, 58, 0, 1024);  // juste à gauche du rectangle
    CHECK(h.state == 5 && h.visible == 0);
    h = clip_horizontal(1024, 58, 0, 1024);  // juste à droite
    CHECK(h.state == 5 && h.visible == 0 && h.right == 58);
    h = clip_horizontal(10, 0, 0, 1024);  // largeur nulle
    CHECK(h.state == 5 && h.visible == 0);
    h = clip_horizontal(10, -3, 0, 1024);  // largeur négative (champ 16 bits signé)
    CHECK(h.state == 5);
    h = clip_horizontal(-10, 2000, 0, 1024);  // plus large que le rectangle : seul le bord gauche compte
    CHECK(h.state == 1 && h.skip_left == 10 && h.right == 2000 && h.visible == 1990);

    auto v = clip_vertical(100, 30, 40, 700);  // entière
    CHECK(v.state == 0 && v.top == 100 && v.rows == 30 && v.skipped == 0 && v.drawn == 30);
    v = clip_vertical(30, 30, 40, 700);  // rognée en haut
    CHECK(v.state == 3 && v.top == 40 && v.skipped == 10 && v.rows == 30 && v.drawn == 20);
    v = clip_vertical(0, 1000, 40, 700);  // rognée en haut, limitée en bas
    CHECK(v.state == 3 && v.skipped == 40 && v.rows == 700 && v.drawn == 660);
    v = clip_vertical(690, 30, 40, 700);  // rognée en bas
    CHECK(v.state == 4 && v.top == 690 && v.rows == 10 && v.drawn == 10);
    v = clip_vertical(10, 30, 40, 700);  // au-dessus
    CHECK(v.state == 5 && v.drawn == 0);
    v = clip_vertical(700, 30, 40, 700);  // au-dessous
    CHECK(v.state == 5 && v.drawn == 0);
    v = clip_vertical(100, 0, 40, 700);  // hauteur nulle
    CHECK(v.state == 5 && v.drawn == 0);
}

// --- aiguillage de l'hôte (Cpu::add_code_jump) ---------------------------------------------------

void test_code_jump() {
    Cpu cpu;
    constexpr uint32_t kCode = 0x00400000, kEnd = kCode + 0x0B;
    cpu.map(kCode, 0x1000, true);
    const uint8_t code[] = {0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1
                            0x83, 0xC0, 0x02,              // add eax, 2
                            0x83, 0xC0, 0x04};             // add eax, 4
    cpu.write(kCode, code, sizeof code);
    int passes = 0;
    cpu.add_code_jump(kCode, 8, [&passes](Cpu& c) -> uint32_t {
        if (++passes == 1) return 0;  // code d'origine, recopié dans le tremplin
        c.set_reg(Cpu::Reg::EAX, 100);
        return kCode + 8;  // les deux premières instructions sont sautées
    });
    CHECK(cpu.read_u8(kCode) == 0xFF && cpu.read_u8(kCode + 6) == 0x90 && cpu.read_u8(kCode + 8) == 0x83);
    cpu.run(kCode, kEnd);
    CHECK(cpu.reg(Cpu::Reg::EAX) == 7);
    cpu.run(kCode, kEnd);
    CHECK(cpu.reg(Cpu::Reg::EAX) == 104);
    CHECK(passes == 2);
    CHECK(throws([&] { cpu.add_code_jump(kCode + 8, 5, [](Cpu&) { return 0u; }); }));
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path(ZEUS_TEST_DIR);
    const std::pair<const char*, std::function<void()>> groups[] = {
        {"lecteur WAV", test_wav},
        {"script d'essai", test_script},
        {"Windows-1252 et UTF-8", test_text},
        {"Vfs", [&] { test_vfs(dir / "vfs"); }},
        {"rognage des sprites", test_clip},
        {"aiguillage de code", test_code_jump},
    };
    for (const auto& [name, test] : groups) {
        const int before = failures;
        try {
            test();
        } catch (const std::exception& e) {
            ++failures;
            std::printf("ÉCHEC : exception %s\n", e.what());
        }
        std::printf("%-24s %s\n", name, failures == before ? "ok" : "ÉCHEC");
    }
    std::printf("%d vérifications, %d échecs\n", checks, failures);
    return failures ? 1 : 0;
}
