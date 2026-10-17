// Mode application : demande le jeu au premier lancement, puis la langue, et lance le jeu.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "tools/zeus_run/zeus_run.h"

namespace zeus::run {
namespace {

// Cherche Zeus.exe dans un dossier du jeu, ou dans son sous-dossier « app » (installateur GOG déballé).
std::string find_game(const std::filesystem::path& folder) {
    namespace fs = std::filesystem;
    for (const fs::path& dir : {folder, folder / "app"}) {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(dir, error)) {
            std::string name = entry.path().filename().string();
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "zeus.exe") return entry.path().string();
        }
    }
    return {};
}

// Jeu livré dans l'application (édition complète) : Contents/Resources/Jeu dans le paquet Mac,
// usr/share/zeus-poseidon/jeu dans l'AppImage. Vide pour une application sans le jeu.
std::string find_bundled_game() {
    const std::filesystem::path base = program_dir().parent_path();
    for (const auto& dir : {base / "Resources" / "Jeu", base / "share" / "zeus-poseidon" / "jeu"}) {
        const std::string exe = find_game(dir);
        if (!exe.empty()) return exe;
    }
    return {};
}

// Dossiers d'installation de Steam : la bibliothèque principale et celles de libraryfolders.vdf.
std::vector<std::filesystem::path> steam_libraries() {
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    if (!home) return {};
    const fs::path h = home;
    std::vector<fs::path> roots{h / "Library/Application Support/Steam", h / ".steam/steam", h / ".local/share/Steam",
                                h / ".var/app/com.valvesoftware.Steam/.local/share/Steam"};
    std::vector<fs::path> libraries = roots;
    const std::regex path_line("\"path\"\\s+\"([^\"]*)\"");
    for (const fs::path& root : roots) {
        std::ifstream in(root / "steamapps" / "libraryfolders.vdf");
        for (std::string line; std::getline(in, line);) {
            std::smatch m;
            if (std::regex_search(line, m, path_line))  // « \\ » échappe chaque barre oblique inverse
                libraries.push_back(std::regex_replace(m[1].str(), std::regex("\\\\\\\\"), "\\"));
        }
    }
    return libraries;
}

// Zeus.exe de l'édition Steam (application 566050) déjà installée, ou vide.
std::string find_steam_game() {
    for (const auto& library : steam_libraries()) {
        const std::string exe = find_game(library / "steamapps" / "common" / "Zeus + Poseidon");
        if (!exe.empty()) return exe;
    }
    return {};
}

// Empreinte SHA-256 d'un fichier, en hexadécimal (vide si illisible).
std::string sha256_hex(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    const auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t block = 0; block < data.size(); block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = static_cast<uint32_t>(data[block + 4 * i]) << 24 | data[block + 4 * i + 1] << 16 |
                   data[block + 4 * i + 2] << 8 | data[block + 4 * i + 3];
        for (int i = 16; i < 64; ++i)
            w[i] = w[i - 16] + (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
                   (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10));
        std::array<uint32_t, 8> v = h;
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = v[7] + (rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) +
                                k[i] + w[i];
            const uint32_t t2 = (rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (int i = 0; i < 8; ++i) h[i] += v[i];
    }
    std::string hex;
    char digits[9];
    for (uint32_t word : h) {
        std::snprintf(digits, sizeof digits, "%08x", word);
        hex += digits;
    }
    return hex;
}

// Seul Zeus.exe essayé ici : celui de l'édition GOG (celui de Steam devrait être le même, #128).
// Un autre est lancé quand même, après un avertissement donné une fois par empreinte.
void check_edition(const std::string& exe, const std::filesystem::path& support) {
    const std::string digest = sha256_hex(exe);
    if (digest.empty() || digest == "382bcd286192a06eb8fdc45f5c37390a19999f5db9c940c3ce2dfcd775e90a57") return;
    const std::filesystem::path memo = support / "autre-edition.txt";
    std::string seen;
    if (std::ifstream in{memo}) std::getline(in, seen);
    std::fprintf(stderr, "Zeus.exe inconnu : SHA-256 %s\n", digest.c_str());
    if (seen == digest) return;
    std::ofstream(memo) << digest << "\n";
    alert(tr("Édition du jeu non essayée", "Untested edition of the game"),
          tr("Ce Zeus.exe n'est pas celui de l'édition GOG, la seule essayée ici : le jeu peut ne pas démarrer. "
             "Merci de signaler son empreinte sur github.com/nic0lasm/zeus-poseidon-arm64/issues/128 :\n\nSHA-256 ",
             "This Zeus.exe is not the GOG edition's, the only one tried here: the game may not start. Please report "
             "its checksum at github.com/nic0lasm/zeus-poseidon-arm64/issues/128:\n\nSHA-256 ") +
              digest);
}

// Outil de déballage des installateurs GOG : celui du paquet, sinon celui du système.
std::string innoextract_path() {
    namespace fs = std::filesystem;
    const fs::path bundled = program_dir().parent_path() / "Helpers" / "innoextract";
    if (fs::exists(bundled)) return bundled.string();
    for (const char* candidate : {"/opt/homebrew/bin/innoextract", "/usr/local/bin/innoextract"})
        if (fs::exists(candidate)) return candidate;
    return find_in_path("innoextract");
}

// Déballe un installateur GOG dans `destination` et renvoie le chemin de Zeus.exe, ou vide.
std::string unpack_installer(const std::string& installer, const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    const std::string tool = innoextract_path();
    if (tool.empty()) {
        alert(tr("Outil de déballage introuvable", "Unpacking tool not found"),
              tr("innoextract n'est pas installé.", "innoextract is not installed."));
        return {};
    }
    if (!find_game(destination).empty()) return find_game(destination);  // déjà déballé
    fs::create_directories(destination);
    busy_start("Zeus + Poseidon", tr("Déballage du jeu en cours (environ une minute, 800 Mo).\n\nCette fenêtre se ferme "
                                     "toute seule, puis le jeu démarre.",
                                     "Unpacking the game (about a minute, 800 MB).\n\nThis window closes by itself, "
                                     "then the game starts."));
    const std::string command = quoted(tool) + " --silent --extract --output-dir " + quoted(destination.string()) +
                                " " + quoted(installer) + " 2>&1";
    const std::string output = command_output(command);
    busy_stop();
    const std::string exe = find_game(destination);
    if (exe.empty())
        alert(tr("Cet installateur n'a pas pu être déballé", "This installer could not be unpacked"),
              tr("Il faut l'installateur GOG de Zeus + Poseidon (setup_zeus_and_poseidon_….exe).",
                 "The GOG installer of Zeus + Poseidon is needed (setup_zeus_and_poseidon_….exe)."));
    if (exe.empty() && !output.empty()) std::fprintf(stderr, "%s\n", output.c_str());
    return exe;
}

}  // namespace

// Mode application : le jeu est demandé une fois (installateur GOG ou dossier déjà déballé),
// puis retenu. `installer` non vide : on le déballe sans rien demander.
int run_app(const std::string& installer) {
    namespace fs = std::filesystem;
    const char* forced = std::getenv("ZEUS_SUPPORT_DIR");
    const fs::path support = forced ? fs::path(forced) : default_support_dir();
    fs::create_directories(support);
    const fs::path memo = support / "dossier-du-jeu.txt", unpacked = support / "Jeu-origine";
    std::string folder;
    if (std::ifstream in{memo}) std::getline(in, folder);
    std::string exe = folder.empty() ? std::string() : find_game(folder);
    // Installateur déposé sur l'icône de l'application, tant que le jeu n'est pas connu.
    const std::string given = !installer.empty() ? installer : exe.empty() ? dropped_file() : std::string();
    if (!given.empty()) {
        exe = unpack_installer(given, unpacked);
        if (exe.empty()) return 1;
        folder = unpacked.string();
    }
    // Jeu livré avec l'application : pris sans rien demander, et pas retenu (le paquet peut être
    // déplacé, et l'AppImage est montée à un endroit différent à chaque lancement).
    bool bundled = false;
    if (exe.empty()) {
        exe = find_bundled_game();
        bundled = !exe.empty();
    }
    // Sans installateur : l'édition Steam déjà installée est prise sans rien demander.
    if (exe.empty()) {
        exe = find_steam_game();
        if (!exe.empty()) folder = fs::path(exe).parent_path().string();
    }
    while (exe.empty()) {
        const std::string quit = tr("Quitter", "Quit"), pick_folder = tr("Dossier…", "Folder…"),
                          pick_installer = tr("Installateur GOG…", "GOG installer…");
        const int choice = ask_choice(
            "Zeus + Poseidon",
            tr("Zeus + Poseidon a besoin du jeu d'origine, que vous devez posséder.\n\nIndiquez l'installateur GOG "
               "(setup_zeus_and_poseidon_….exe) : il sera déballé, ce qui prend environ une minute et 800 Mo. Ou bien "
               "indiquez un dossier où le jeu est déjà déballé, ou installé par Steam.",
               "Zeus + Poseidon needs the original game, which you must own.\n\nPick the GOG installer "
               "(setup_zeus_and_poseidon_….exe): it will be unpacked, which takes about a minute and 800 MB. Or pick "
               "a folder where the game is already unpacked, or installed by Steam."),
            {quit, pick_folder, pick_installer}, 2);
        if (choice == 2) {
            const std::string file = ask_file(tr("Installateur GOG de Zeus + Poseidon", "GOG installer of Zeus + Poseidon"));
            if (file.empty()) continue;
            exe = unpack_installer(file, unpacked);
            folder = unpacked.string();
        } else if (choice == 1) {
            folder = ask_folder(tr("Dossier du jeu (celui qui contient Zeus.exe)", "Game folder (the one that contains Zeus.exe)"));
            if (folder.empty()) continue;
            exe = find_game(folder);
            if (exe.empty())
                alert(tr("Zeus.exe est introuvable dans ce dossier", "Zeus.exe was not found in this folder"),
                      tr("Choisissez le dossier où le jeu a été déballé.", "Pick the folder the game was unpacked into."));
        } else {
            return 0;  // Quitter, ou dialogue fermé
        }
    }
    if (!bundled) std::ofstream(memo) << folder << "\n";
    check_edition(exe, support);
    RunOptions o;
    o.exe = exe;
    o.write_dir = (support / "Jeu").string();
    o.wide = "auto";

    // Langue : demandée au premier lancement, puis retenue dans langue.txt (ligne vide : l'anglais
    // de l'édition GOG). Sans aucun pack, on ne demande rien : l'anglais. Garder la touche Option
    // enfoncée au lancement ouvre la liste des langues. Les packs
    // sont rangés dans « Packs » ; chaque langue a ses sauvegardes.
    const fs::path packs_dir = support / "Packs", choice_file = support / "langue.txt";
    std::string choice;
    bool chosen = static_cast<bool>(std::ifstream(choice_file));
    if (std::ifstream in{choice_file}) std::getline(in, choice);
    const bool ask_again = option_key_down();
    if (ask_again) chosen = false;
    while (!chosen) {
        std::vector<std::string> packs;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(packs_dir, error))
            if (fs::exists(entry.path() / "remplace.txt")) packs.push_back(entry.path().filename().string());
        std::sort(packs.begin(), packs.end());
        if (packs.empty() && !ask_again) break;  // aucun pack et pas d'Option : l'anglais, sans question
        const std::string build = tr("Autre langue, depuis les CD d'origine…", "Another language, from the original CDs…");
        std::vector<std::string> items{"English"};
        items.insert(items.end(), packs.begin(), packs.end());
        items.push_back(build);
        const std::string answer = ask_list("Zeus + Poseidon", tr("Langue du jeu", "Game language"), items);
        if (answer.empty()) return 0;  // annulé
        if (answer == build) {
            build_language_pack(fs::path(exe).parent_path(), packs_dir);
            continue;  // le nouveau pack apparaît dans la liste
        }
        choice = answer == "English" ? "" : answer;
        std::ofstream(choice_file) << choice << "\n";
        chosen = true;
    }
    if (!choice.empty() && fs::exists(packs_dir / choice / "remplace.txt")) {
        o.language = (packs_dir / choice).string();
        o.write_dir = (support / ("Jeu-" + choice)).string();
    }
    return run(o);
}

}  // namespace zeus::run
