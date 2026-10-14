// Commandes du système, communes à toutes les plateformes (dialogues : dialogs_macos.cpp, dialogs_sdl.cpp).

#include <cstdio>

#include <string>

#include "tools/zeus_run/zeus_run.h"

namespace zeus::run {

// Lance une commande et renvoie sa sortie, sans le saut de ligne final.
std::string command_output(const std::string& command) {
    std::string out;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof buffer, pipe)) out += buffer;
        pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Les dialogues du mode application sont en français ou en anglais.
const char* tr(const char* fr, const char* en) {
    static const bool french = system_is_french();
    return french ? fr : en;
}

// Entoure un texte d'apostrophes pour le passer à l'interpréteur de commandes.
std::string quoted(const std::string& text) {
    std::string out = "'";
    for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string find_in_path(const std::string& name) {
    return command_output("command -v " + quoted(name) + " 2>/dev/null");
}

}  // namespace zeus::run
