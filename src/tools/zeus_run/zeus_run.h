// Déclarations partagées par les fichiers de zeus-run (options : main.cpp).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zeus::run {

struct RunOptions {
    std::string exe, write_dir = "run/zeus", profile, frame_log, memory_dump, wide, watch, language, screenshot, window_shot, script, trace_only, break_on, count, state_log, verify;
    bool trace = false, ticks = false, headless = false, counts = false, mute = false, fullscreen = false, deterministic = false,
         native = true;
    int frames = 0;
    uint32_t verify_every = 1;
    double seconds = 0;
};

// launch.cpp : exécute le jeu dans le processeur émulé.
int run(const RunOptions& o);

// diagnostics.cpp : --selftest, --bench, --inspect, --check-audio.
int selftest();
int bench();
int inspect(const char* path);
int check_audio(const std::filesystem::path& root);

// app.cpp : mode application.
int run_app(const std::string& installer);

// packs.cpp : fabrication d'un pack de langue depuis les CD d'origine.
std::string make_language_pack(const std::filesystem::path& cd_folder, const std::filesystem::path& game_dir,
                               const std::filesystem::path& pack);
void build_language_pack(const std::filesystem::path& game_dir, const std::filesystem::path& packs_dir);

// dialogs.cpp : commandes du système, communes à toutes les plateformes.
std::string command_output(const std::string& command);
const char* tr(const char* fr, const char* en);
std::string quoted(const std::string& text);

// dialogs_macos.cpp (AppleScript, CoreGraphics) ou dialogs_sdl.cpp (SDL, zenity/kdialog, ligne de commande).
bool system_is_french();
void alert(const std::string& title, const std::string& text);
// Boutons de gauche à droite ; renvoie l'indice choisi, ou -1 si le dialogue est fermé.
int ask_choice(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
               int default_button);
// Sélecteur de fichier ouvert dans Téléchargements (macOS : sans demande d'accès aux dossiers protégés).
std::string ask_file(const std::string& prompt);    // vide : annulé
std::string ask_folder(const std::string& prompt);  // vide : annulé
// Liste à choix unique ; renvoie l'élément choisi, vide si annulé.
std::string ask_list(const std::string& title, const std::string& prompt, const std::vector<std::string>& items);
std::string ask_text(const std::string& prompt, const std::string& initial);  // vide : annulé
void notify(const std::string& text);  // notification discrète, sans réponse
// Retour visible pendant une longue attente : une fenêtre qui se ferme à la fin (ou un message).
void busy_start(const std::string& title, const std::string& text);
void busy_stop();
// Fichier déposé sur l'icône de l'application (ou ouvert avec elle) au lancement, ou vide.
std::string dropped_file();
bool option_key_down();  // touche Option (Alt) tenue au lancement
std::filesystem::path default_support_dir();
std::filesystem::path program_dir();
std::string find_in_path(const std::string& name);  // chemin d'un outil installé, ou vide

}  // namespace zeus::run
