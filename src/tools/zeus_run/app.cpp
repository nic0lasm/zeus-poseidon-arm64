// Mode application : demande le jeu au premier lancement, puis la langue, et lance le jeu.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    while (exe.empty()) {
        const std::string quit = tr("Quitter", "Quit"), pick_folder = tr("Dossier…", "Folder…"),
                          pick_installer = tr("Installateur GOG…", "GOG installer…");
        const int choice = ask_choice(
            "Zeus + Poseidon",
            tr("Zeus + Poseidon a besoin du jeu d'origine, que vous devez posséder.\n\nIndiquez l'installateur GOG "
               "(setup_zeus_and_poseidon_….exe) : il sera déballé, ce qui prend environ une minute et 800 Mo. Ou bien "
               "indiquez un dossier où le jeu est déjà déballé.",
               "Zeus + Poseidon needs the original game, which you must own.\n\nPick the GOG installer "
               "(setup_zeus_and_poseidon_….exe): it will be unpacked, which takes about a minute and 800 MB. Or pick "
               "a folder where the game is already unpacked."),
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
    std::ofstream(memo) << folder << "\n";
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
