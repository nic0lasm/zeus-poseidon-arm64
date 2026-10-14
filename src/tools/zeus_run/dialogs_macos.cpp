// Dialogues du mode application sur macOS : AppleScript et CoreGraphics.

#include <CoreGraphics/CoreGraphics.h>
#include <SDL.h>
#include <signal.h>
#include <mach-o/dyld.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "tools/zeus_run/zeus_run.h"

namespace zeus::run {
namespace {

// Exécute un script AppleScript d'une ligne et renvoie ce qu'il affiche.
std::string applescript(const std::string& script) {
    return command_output("/usr/bin/osascript -e " + quoted(script) + " 2>/dev/null");
}

// Texte entre guillemets AppleScript.
std::string literal(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

}  // namespace

bool system_is_french() {
    return command_output("/usr/bin/defaults read -g AppleLanguages 2>/dev/null").find("\"fr") != std::string::npos;
}

void alert(const std::string& title, const std::string& text) {
    applescript("display alert " + literal(title) + " message " + literal(text));
}

int ask_choice(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
               int default_button) {
    std::string list;
    for (const std::string& b : buttons) list += (list.empty() ? "" : ", ") + literal(b);
    const std::string answer = applescript("button returned of (display dialog " + literal(text) + " buttons {" + list +
                                           "} default button " + std::to_string(default_button + 1) + " with title " +
                                           literal(title) + ")");
    for (size_t i = 0; i < buttons.size(); ++i)
        if (buttons[i] == answer) return static_cast<int>(i);
    return -1;
}

std::string ask_file(const std::string& prompt) {
    // Téléchargements, où se trouve l'installateur : « path to downloads folder » n'appelle pas
    // la demande d'accès aux dossiers protégés.
    return applescript("POSIX path of (choose file with prompt " + literal(prompt) +
                       " of type {\"exe\"} default location (path to downloads folder))");
}

std::string ask_folder(const std::string& prompt) {
    return applescript("POSIX path of (choose folder with prompt " + literal(prompt) + ")");
}

std::string ask_list(const std::string& title, const std::string& prompt, const std::vector<std::string>& items) {
    std::string list;
    for (const std::string& item : items) list += (list.empty() ? "" : ", ") + literal(item);
    const std::string answer = applescript("choose from list {" + list + "} with title " + literal(title) +
                                           " with prompt " + literal(prompt) + " default items {" +
                                           literal(items.front()) + "}");
    return answer == "false" ? std::string() : answer;
}

std::string ask_text(const std::string& prompt, const std::string& initial) {
    return applescript("text returned of (display dialog " + literal(prompt) + " default answer " + literal(initial) +
                       " with title \"Zeus + Poseidon\")");
}

void notify(const std::string& text) {
    applescript("display notification " + literal(text) + " with title \"Zeus + Poseidon\"");
}

namespace {
long busy_pid = 0;
}

// Le dialogue tourne dans un processus à part, tué par busy_stop ; son seul bouton le masque.
void busy_start(const std::string& title, const std::string& text) {
    busy_stop();
    const std::string script = "display dialog " + literal(text) + " buttons {" + literal(tr("Masquer", "Hide")) +
                               "} default button 1 with title " + literal(title) + " giving up after 3600";
    busy_pid = std::atol(command_output("/usr/bin/osascript -e " + quoted(script) + " >/dev/null 2>&1 & echo $!").c_str());
}

void busy_stop() {
    if (busy_pid > 0) kill(static_cast<pid_t>(busy_pid), SIGTERM);
    busy_pid = 0;
}

// Finder transmet le fichier déposé sur l'icône par un événement « ouvrir », que SDL reçoit une
// fois initialisé : on l'attend un instant, sans fenêtre.
std::string dropped_file() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return {};
    std::string file;
    const Uint32 end = SDL_GetTicks() + 500;
    while (file.empty() && SDL_GetTicks() < end) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_DROPFILE) {
                if (file.empty()) file = event.drop.file;
                SDL_free(event.drop.file);
            }
        SDL_Delay(10);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return file;
}

bool option_key_down() {
    return CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskAlternate;
}

std::filesystem::path default_support_dir() {
    const char* home = std::getenv("HOME");
    if (!home) throw std::runtime_error("dossier personnel introuvable");
    return std::filesystem::path(home) / "Library/Application Support/Zeus Poseidon";
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
