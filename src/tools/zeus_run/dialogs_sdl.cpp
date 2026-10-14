// Dialogues du mode application hors macOS : boîtes de message SDL, sélecteur de fichiers de
// zenity ou kdialog s'il est installé, sinon questions sur la ligne de commande.

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tools/zeus_run/zeus_run.h"

namespace zeus::run {
namespace {

// Question sur la ligne de commande : renvoie l'indice (0 = premier) ou -1 si l'entrée est fermée.
int ask_in_terminal(const std::string& text, const std::vector<std::string>& options) {
    std::printf("%s\n", text.c_str());
    for (size_t i = 0; i < options.size(); ++i) std::printf("  %zu) %s\n", i + 1, options[i].c_str());
    for (;;) {
        std::printf("> ");
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) return -1;
        const int n = std::atoi(line.c_str());
        if (n >= 1 && n <= static_cast<int>(options.size())) return n - 1;
    }
}

std::string ask_path(const std::string& prompt, bool folder) {
    std::string out;
    if (!find_in_path("zenity").empty())
        out = command_output("zenity --file-selection " + std::string(folder ? "--directory " : "") + "--title " +
                             quoted(prompt) + " 2>/dev/null");
    else if (!find_in_path("kdialog").empty())
        out = command_output("kdialog " + std::string(folder ? "--getexistingdirectory . " : "--getopenfilename . ") +
                             "--title " + quoted(prompt) + " 2>/dev/null");
    else {
        std::printf("%s\n> ", prompt.c_str());
        std::fflush(stdout);
        std::getline(std::cin, out);
    }
    return out;
}

}  // namespace

bool system_is_french() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value && *value) return std::string(value).rfind("fr", 0) == 0;
    }
    return false;
}

void alert(const std::string& title, const std::string& text) {
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title.c_str(), text.c_str(), nullptr) != 0)
        std::fprintf(stderr, "%s\n%s\n", title.c_str(), text.c_str());
}

int ask_choice(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
               int default_button) {
    std::vector<SDL_MessageBoxButtonData> data;
    for (size_t i = 0; i < buttons.size(); ++i) {
        Uint32 flags = 0;
        if (static_cast<int>(i) == default_button) flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i == 0) flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        data.push_back({flags, static_cast<int>(i), buttons[i].c_str()});
    }
    SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, nullptr, title.c_str(), text.c_str(),
                           static_cast<int>(data.size()), data.data(), nullptr};
    int id = -1;
    if (SDL_ShowMessageBox(&box, &id) == 0) return id;
    return ask_in_terminal(title + "\n\n" + text, buttons);  // pas d'écran : ligne de commande
}

std::string ask_file(const std::string& prompt) { return ask_path(prompt, false); }
std::string ask_folder(const std::string& prompt) { return ask_path(prompt, true); }

std::string ask_list(const std::string& title, const std::string& prompt, const std::vector<std::string>& items) {
    const int i = ask_choice(title, prompt, items, 0);
    return i < 0 ? std::string() : items[static_cast<size_t>(i)];
}

std::string ask_text(const std::string& prompt, const std::string& initial) {
    std::string out;
    if (!find_in_path("zenity").empty())
        out = command_output("zenity --entry --title 'Zeus + Poseidon' --text " + quoted(prompt) + " --entry-text " +
                             quoted(initial) + " 2>/dev/null");
    else {
        std::printf("%s [%s]\n> ", prompt.c_str(), initial.c_str());
        std::fflush(stdout);
        std::getline(std::cin, out);
        if (out.empty()) out = initial;
    }
    return out;
}

void notify(const std::string& text) { std::fprintf(stderr, "%s\n", text.c_str()); }

bool option_key_down() {
    // Sans le clavier du système sous la main : variable d'environnement, ou Alt tenue si SDL le voit.
    if (const char* ask = std::getenv("ZEUS_ASK_LANGUAGE"); ask && *ask) return true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
    SDL_PumpEvents();
    return (SDL_GetModState() & KMOD_ALT) != 0;
}

std::filesystem::path default_support_dir() {
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "Zeus Poseidon";
    const char* home = std::getenv("HOME");
    if (!home) throw std::runtime_error("dossier personnel introuvable");
    return std::filesystem::path(home) / ".local/share/Zeus Poseidon";
}

// Dossier du programme en cours d'exécution.
std::filesystem::path program_dir() {
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
}

}  // namespace zeus::run
