// Commandes du système et dialogues AppleScript du mode application.

#include <cstdio>
#include <mach-o/dyld.h>

#include <filesystem>
#include <string>

#include "tools/zeus_run/zeus_run.h"

namespace zeus::run {
namespace {

// Vrai si la langue du système est le français : les dialogues du mode application sont
// en français ou en anglais.
bool french() {
    static const bool value = command_output("/usr/bin/defaults read -g AppleLanguages 2>/dev/null").find("\"fr") != std::string::npos;
    return value;
}

}  // namespace

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

const char* tr(const char* fr, const char* en) { return french() ? fr : en; }

// Entoure un texte d'apostrophes pour le passer à l'interpréteur de commandes.
std::string quoted(const std::string& text) {
    std::string out = "'";
    for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

// Exécute un script AppleScript d'une ligne et renvoie ce qu'il affiche.
std::string applescript(const std::string& script) {
    return command_output("/usr/bin/osascript -e " + quoted(script) + " 2>/dev/null");
}

void alert(const std::string& title, const std::string& text) {
    applescript("display alert \"" + title + "\" message \"" + text + "\"");
}

// Dossier du programme en cours d'exécution.
std::filesystem::path program_dir() {
    char self[4096];
    uint32_t size = sizeof self;
    std::error_code error;
    if (_NSGetExecutablePath(self, &size) != 0) return {};
    return std::filesystem::canonical(self, error).parent_path();
}

}  // namespace zeus::run
