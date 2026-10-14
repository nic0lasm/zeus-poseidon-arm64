// Fabrication d'un pack de langue depuis les CD d'origine (--make-pack et mode application).

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

#include "tools/zeus_run/zeus_run.h"

namespace zeus::run {

// Fabrique un pack de langue à partir des CD d'origine, avec les outils Python du projet :
// ceux du paquet (et son interpréteur) s'ils existent, sinon ceux du dépôt. Renvoie un texte
// d'erreur, vide en cas de succès.
std::string make_language_pack(const std::filesystem::path& cd_folder, const std::filesystem::path& game_dir,
                               const std::filesystem::path& pack) {
    namespace fs = std::filesystem;
    const fs::path here = program_dir();
    fs::path python = here.parent_path() / "Resources" / "python" / "bin" / "python3";
    fs::path tool = here.parent_path() / "Resources" / "tools" / "make_pack_from_cds.py";
    if (!fs::exists(tool)) {
        python = "/usr/bin/python3";
        tool = here.parent_path() / "tools" / "make_pack_from_cds.py";
    }
    if (!fs::exists(tool) || !fs::exists(python)) return tr("outils de fabrication absents", "pack building tools missing");
    const std::string command = quoted(python.string()) + " " + quoted(tool.string()) + " " + quoted(cd_folder.string()) +
                                " " + quoted(game_dir.string()) + " " + quoted(pack.string()) + " 2>&1";
    std::string last;
    bool done = false;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[1024];
        while (std::fgets(buffer, sizeof buffer, pipe)) {
            std::string line = buffer;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (line.empty()) continue;
            std::fprintf(stderr, "%s\n", line.c_str());
            if (line == "TERMINÉ") done = true;
            else last = line;
        }
        pclose(pipe);
    }
    if (done && fs::exists(pack / "remplace.txt")) return {};
    std::error_code error;
    fs::remove_all(pack, error);  // pack inachevé, que l'outil vient de créer
    return last.empty() ? std::string(tr("échec", "failure")) : last;
}

// Dialogues de fabrication d'un pack, depuis le mode application.
void build_language_pack(const std::filesystem::path& game_dir, const std::filesystem::path& packs_dir) {
    namespace fs = std::filesystem;
    const std::string folder = ask_folder(
        tr("Dossier qui contient les images des deux CD (Zeus et Poséidon) et, si possible, les mises à jour 1.1 et 2.1",
           "Folder that holds the images of both CDs (Zeus and Poseidon) and, if possible, the 1.1 and 2.1 updates"));
    if (folder.empty()) return;
    const std::string name = ask_text(tr("Nom de cette langue dans la liste", "Name of this language in the list"), "Français");
    std::string safe;
    for (unsigned char c : name)
        if (std::isalnum(c) || c >= 0x80 || c == '-' || c == '_' || c == ' ') safe += static_cast<char>(c);
    if (safe.empty()) return;
    std::error_code error;
    fs::create_directories(packs_dir, error);
    if (fs::exists(packs_dir / safe)) {
        alert(tr("Ce nom est déjà pris", "This name is already taken"), safe);
        return;
    }
    notify(tr("Fabrication du pack de langue : environ une minute, 3 Go d'espace temporaire.",
              "Building the language pack: about a minute, 3 GB of temporary space."));
    const std::string failure = make_language_pack(folder, game_dir, packs_dir / safe);
    if (failure.empty()) {
        notify(tr("Pack de langue prêt : ", "Language pack ready: ") + safe);
        return;
    }
    std::string reason;
    for (char c : failure) reason += c == '"' || c == '\\' ? '\'' : c;
    alert(tr("Le pack n'a pas pu être fabriqué", "The pack could not be built"), reason);
}

}  // namespace zeus::run
