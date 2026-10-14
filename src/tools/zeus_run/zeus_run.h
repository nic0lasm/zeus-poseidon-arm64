// Déclarations partagées par les fichiers de zeus-run (options : main.cpp).

#pragma once

#include <filesystem>
#include <string>

namespace zeus::run {

struct RunOptions {
    std::string exe, write_dir = "run/zeus", profile, frame_log, memory_dump, wide, watch, language, screenshot, window_shot, script, trace_only, break_on, count;
    bool trace = false, ticks = false, headless = false, counts = false, mute = false, fullscreen = false, deterministic = false,
         native = true;
    int frames = 0;
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

// dialogs.cpp : commandes du système et dialogues AppleScript.
std::string command_output(const std::string& command);
const char* tr(const char* fr, const char* en);
std::string quoted(const std::string& text);
std::string applescript(const std::string& script);
void alert(const std::string& title, const std::string& text);
std::filesystem::path program_dir();

}  // namespace zeus::run
